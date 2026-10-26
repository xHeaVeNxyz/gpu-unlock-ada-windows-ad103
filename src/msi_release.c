/* SPDX-License-Identifier: GPL-2.0-only */
/* MSI Z490/AD104 ownership adapter. Snapshot/delta strategy adapted from
 * impacifist/Windows-GPU-UNLOCK 9352abd internal-boot/boot/prepare-console.c.
 * CAP0005 identifies the two observed driver owners, but not their binary hashes.
 * No driver code/data patch.
 * No firmware variables, controller reconnects or reset fallback are used.
 */
#include "msi_release.h"
#include "pci_decode_guard.h"
#include "boot_io.h"
#include "profile.h"
#include "sha256.h"
#include "compat/compat.h"
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>
#include <IndustryStandard/Acpi10.h>

#define MAX_EDGES 4096
#define GU_MAX_FOREIGN_GOPS 4
#define GU_MAX_PATH_BYTES 512
typedef struct {EFI_HANDLE handle,agent,controller;EFI_GUID protocol;UINT32 attributes,count;} EDGE;
typedef struct {UINTN count;EDGE edges[MAX_EDGES];} GRAPH;
/* Stage48: foreign GOPs are only admissible when rooted at a verified AMD
 * PCI display controller. Preserve their handles and mode snapshots. */
typedef struct {
  EFI_HANDLE handle;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *interface;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
} FOREIGN_GOP;
typedef struct {
  EFI_SYSTEM_TABLE *table;EFI_HANDLE self,pci,gop,gop_text,gop_gfx,out_agent,gfx_agent,nv_agent;
  EFI_HANDLE console,input,error,pointer_agent,key_agent,usb_host,usb_agent;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *text,*err;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL *keys;
  UINTN key_reset,key_read;EFI_EVENT key_wait;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop_interface;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
  UINT32 config[64];UINT64 attributes;
  EFI_HANDLE amd_pci;
  FOREIGN_GOP foreign[GU_MAX_FOREIGN_GOPS];
  UINTN foreign_count;
  UINTN stage,reported_additions,reported_removals;BOOLEAN ready,mutated,claimed;
} PLAN;
static PLAN Plan;
static GRAPH Original,Current;
static BOOLEAN OriginalUsb[MAX_EDGES];
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,TextGuid=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
static EFI_GUID InputGuid=EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
static EFI_GUID PointerGuid={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0,0x90,0x27,0x3f,0xc1,0x4d}};
static EFI_GUID UsbHcGuid={0x3e745226,0x9818,0x45b6,{0xa2,0xac,0xd7,0xcd,0x0e,0x8b,0xa2,0xbc}};
static const UINT8 UsbHostPath[]={2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,0,0x14,127,255,4,0};
static EFI_GUID BindingGuid=EFI_DRIVER_BINDING_PROTOCOL_GUID,ImageGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
/* Stage 17 read-only pin update: PCI path bytes are taken from the actual
 * target display_devices[0].path.hex in CAP0004.JSON, including the two
 * PCI nodes after the ACPI root. The previously borrowed path had byte
 * index 17 == 0x03; CAP0004 has byte index 17 == 0x01.
 * This correction does NOT qualify UEFI driver ownership, firmware
 * images, GPU reset, DMA, GOP recovery or any Windows handoff.
 */
static const UINT8 PciPath[]={2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,1,1,1,1,6,0,0,0,127,255,4,0};
/* No additional GOP path pin: CAP0004 observes ONE GOP on NVIDIA.
 * Do not infer that an upstream two-child topology applies on this board. */
static const UINT8 OutFile[]={4,6,20,0,0x7d,0x49,0x8a,0x62,0xf6,0x2b,0x64,0x42,0x87,0x41,6,0x9d,0xbd,0x33,0x99,0xd6,127,255,4,0};
static const UINT8 GfxFile[]={4,6,20,0,0xdd,0xab,0xe7,0x43,0x52,0xe3,0xfb,0x4c,0xa2,0x30,0x4c,0xdc,0x1d,0x35,0x0e,0x5c,127,255,4,0};
static const UINT8 NvFile[]={4,4,28,0,'n',0,'v',0,'g',0,'o',0,'p',0,'-',0,'a',0,'d',0,'1',0,'x',0,'x',0,0,0,127,255,4,0};

static BOOLEAN Equal(const VOID *a,const VOID *b,UINTN n){return !memcmp(a,b,n);}
static BOOLEAN Guid(const EFI_GUID *a,const EFI_GUID *b){return Equal(a,b,16);}
static EFI_STATUS Interface(EFI_HANDLE h,EFI_GUID *g,VOID **p) {
  *p=NULL;EFI_STATUS s=Plan.table->BootServices->HandleProtocol(h,g,p);
  return s==EFI_SUCCESS&&!*p?EFI_COMPROMISED_DATA:s;
}
/* Match exact, bounded, single-instance paths; never accept a prefix alone. */
static BOOLEAN Path(EFI_HANDLE h,const UINT8 *bytes,UINTN count) {
  EFI_DEVICE_PATH_PROTOCOL *p=NULL;
  if(Interface(h,&PathGuid,(VOID **)&p)!=EFI_SUCCESS)return FALSE;
  UINTN at=0;
  while(at+4<=count) {
    UINTN n=p->Length[0]+((UINTN)p->Length[1]<<8);
    if(n<4||n>count-at||!Equal(p,bytes+at,n))return FALSE;
    if(p->Type==END_DEVICE_PATH_TYPE)return n==4&&p->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE&&at+4==count;
    at+=n;p=(VOID *)((UINT8 *)p+n);
  }
  return FALSE;
}
/* The upstream code accepted GPU display children below the GPU-root path.
 * This is only a structural check. CAP0004 observes one GOP and does not
 * prove safe disconnect/reconnect, driver identity, or display recovery. */
/* CAP0005 LoadedImage.FilePath exact-node comparison, diagnostic only.
 * Never dereference beyond the expected number of path bytes. The firmware
 * controls these pointers: this is structural evidence, NOT a trusted hash. */
static BOOLEAN ImagePath(const EFI_LOADED_IMAGE_PROTOCOL *im,const UINT8 *bytes,UINTN count) {
  if(!im||!im->FilePath||!bytes||count<4||count>128)return FALSE;
  const UINT8 *p=(const UINT8 *)im->FilePath;
  UINTN at=0;
  while(at+4<=count) {
    UINTN n=(UINTN)bytes[at+2]|((UINTN)bytes[at+3]<<8);
    if(n<4||n>count-at)return FALSE;
    if(!Equal(p+at,bytes+at,n))return FALSE;
    if(bytes[at]==END_DEVICE_PATH_TYPE)
      return bytes[at+1]==END_ENTIRE_DEVICE_PATH_SUBTYPE&&n==4&&at+4==count;
    at+=n;
  }
  return FALSE;
}
static BOOLEAN GpuChild(EFI_HANDLE h) {
  EFI_DEVICE_PATH_PROTOCOL *p=NULL;
  if(Interface(h,&PathGuid,(VOID **)&p)!=EFI_SUCCESS||!p)return FALSE;
  if(!Equal(p,PciPath,sizeof(PciPath)-4))return FALSE;
  const UINT8 *at=(const UINT8 *)p+sizeof(PciPath)-4;
  for(UINTN n=0;n<8;n++){
    UINTN len=(UINTN)at[2]|((UINTN)at[3]<<8);
    if(len<4||len>64)return FALSE;
    if(at[0]==END_DEVICE_PATH_TYPE)
      return len==4&&at[1]==END_ENTIRE_DEVICE_PATH_SUBTYPE;
    at+=len;
  }
  return FALSE;
}
static EFI_STATUS Snapshot(GRAPH *g) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_HANDLE *handles=NULL;UINTN count=0;g->count=0;
  EFI_STATUS s=b->LocateHandleBuffer(AllHandles,NULL,NULL,&count,&handles);
  if(s==EFI_SUCCESS&&(!handles||!count||count>4096))s=EFI_BAD_BUFFER_SIZE;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_GUID **protocols=NULL;UINTN pc=0;
    s=b->ProtocolsPerHandle(handles[i],&protocols,&pc);
    if(s==EFI_SUCCESS&&(!protocols||!pc||pc>64))s=EFI_BAD_BUFFER_SIZE;
    for(UINTN j=0;s==EFI_SUCCESS&&j<pc;j++) {
      EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN ec=0;
      if(!protocols[j]){s=EFI_COMPROMISED_DATA;break;}
      s=b->OpenProtocolInformation(handles[i],protocols[j],&entries,&ec);
      if(s==EFI_SUCCESS&&(ec>128||(ec&&!entries)))s=EFI_BAD_BUFFER_SIZE;
      for(UINTN k=0;s==EFI_SUCCESS&&k<ec;k++) {
        EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e=&entries[k];
        if(e->Attributes==1||e->Attributes==2||e->Attributes==4)continue;
        BOOLEAN own=Plan.claimed&&handles[i]==Plan.pci&&e->AgentHandle==Plan.self&&
          !e->ControllerHandle&&e->Attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE&&e->OpenCount==1;
        if(g->count==MAX_EDGES||!e->AgentHandle||(!e->ControllerHandle&&!own)||!e->OpenCount){s=EFI_COMPROMISED_DATA;break;}
        g->edges[g->count++]=(EDGE){handles[i],e->AgentHandle,e->ControllerHandle,*protocols[j],e->Attributes,e->OpenCount};
      }
      if(entries&&b->FreePool(entries)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
    }
    if(protocols&&b->FreePool(protocols)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  }
  if(handles&&b->FreePool(handles)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  return s;
}
static BOOLEAN SameEdge(const EDGE *a,const EDGE *b) {
  return a->handle==b->handle&&a->agent==b->agent&&a->controller==b->controller&&
    a->attributes==b->attributes&&a->count==b->count&&Guid(&a->protocol,&b->protocol);
}
/* Stage56: ConSplitter is now positively identified by FV-file GUID
 * 628a497d-2bf6-4264-8741-069dbd3399d6 (Stage55 trace).
 * Stage 1: remove ConSplitter's two TEXT edges on target NVIDIA GOP child.
 * Stage 2: remove GraphicsConsole's BY_DRIVER GOP edge.
 * Stage 3: NVIDIA PCI root removal deletes only the NVIDIA child edges.
 * Do NOT disconnect ConSplitter as an image or from the global virtual
 * console; DisconnectController targets only its NVIDIA child controller.
 */
static BOOLEAN Removed(const EDGE *e,UINTN stage) {
  if(stage>=1&&Plan.out_agent&&e->handle==Plan.gop_text&&
     e->agent==Plan.out_agent&&Guid(&e->protocol,&TextGuid)&&
     ((e->attributes==EFI_OPEN_PROTOCOL_BY_DRIVER&&e->controller==Plan.gop_text)||
      (e->attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER&&e->controller==Plan.console)))
    return TRUE;
  if(stage>=2&&Plan.gfx_agent&&e->handle==Plan.gop_gfx&&
     e->agent==Plan.gfx_agent&&Guid(&e->protocol,&GopGuid)&&
     e->attributes==EFI_OPEN_PROTOCOL_BY_DRIVER&&e->controller==Plan.gop_gfx)
    return TRUE;
  return stage>=3&&(e->handle==Plan.pci||GpuChild(e->handle));
}
/* Original unrelated relationships must remain present. New relationships can
 * appear during USB enumeration; they cannot involve any protected endpoint.
 * A protocol GUID alone never authorizes access to the GPU or its owners. */
/* Stage49: protect the verified failover AMD display root and its GOP children
 * from unexpected protocol-open deltas. Gops() validates the GOP protocol and
 * display path/mode, but that alone cannot guard new graph edges targeting AMD.
 * This adds NO UEFI calls; it is a policy predicate used by Expected(). */
static BOOLEAN ProtectedHandle(EFI_HANDLE h) {
  if(!h)return FALSE;
  if(h==Plan.pci||h==Plan.gop||h==Plan.self||h==Plan.out_agent||
     h==Plan.gfx_agent||h==Plan.nv_agent||h==Plan.console||h==Plan.input||
     h==Plan.error||h==Plan.amd_pci)return TRUE;
  for(UINTN i=0;i<Plan.foreign_count;i++)
    if(h==Plan.foreign[i].handle)return TRUE;
  return FALSE;
}
static BOOLEAN ProtectedEdge(const EDGE *e) {
  return ProtectedHandle(e->handle)||ProtectedHandle(e->agent)||ProtectedHandle(e->controller);
}
static BOOLEAN OwnEdge(const EDGE *e) {
  return e->handle==Plan.pci&&e->agent==Plan.self&&!e->controller&&
    Guid(&e->protocol,&PciGuid)&&e->attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE&&e->count==1;
}
static BOOLEAN KnownEdge(const EDGE *e,UINTN stage) {
  for(UINTN i=0;i<Original.count;i++)
    if(!Removed(&Original.edges[i],stage)&&SameEdge(&Original.edges[i],e))return TRUE;
  return FALSE;
}
/* A late USB mouse attaches to the existing virtual console (RUN0005).
 * Only the preexisting pointer splitter may add this exact child relationship,
 * and it must also own that physical pointer BY_DRIVER on a non-GPU USB path.
 * No exception is based on a GUID prefix or on a raw handle address alone. */
static BOOLEAN UsbPointerPath(EFI_HANDLE h) {
  static const UINT8 host[]={2,1,12,0,0xd0,0x41,3,10,0,0,0,0,1,1,6,0,0,0x14};
  EFI_DEVICE_PATH_PROTOCOL *p=NULL;
  if(Interface(h,&PathGuid,(VOID **)&p)!=EFI_SUCCESS||!Equal(p,host,sizeof(host)))return FALSE;
  UINT8 *at=(UINT8 *)p+sizeof(host);
  for(UINTN depth=0;depth<8;depth++) {
    if(at[0]!=MESSAGING_DEVICE_PATH||at[1]!=MSG_USB_DP||at[2]!=6||at[3]!=0)return FALSE;
    at+=6;
    if(at[0]==END_DEVICE_PATH_TYPE)return at[1]==END_ENTIRE_DEVICE_PATH_SUBTYPE&&at[2]==4&&at[3]==0;
  }
  return FALSE;
}
static BOOLEAN PointerOwner(const GRAPH *g,const EDGE *e) {
  UINTN owners=0;
  for(UINTN i=0;i<g->count;i++) {
    const EDGE *o=&g->edges[i];
    if(o->handle==e->handle&&Guid(&o->protocol,&e->protocol)&&
       o->attributes==EFI_OPEN_PROTOCOL_BY_DRIVER) {
      if(o->agent!=e->agent||o->controller!=e->handle||o->count!=1)return FALSE;
      owners++;
    }
  }
  return owners==1;
}
static BOOLEAN PointerAddition(const EDGE *e) {
  BOOLEAN splitter=(Guid(&e->protocol,&PointerGuid)&&Plan.pointer_agent&&e->agent==Plan.pointer_agent)||
    (Guid(&e->protocol,&InputGuid)&&Plan.key_agent&&e->agent==Plan.key_agent);
  return splitter&&e->controller==Plan.console&&
    !ProtectedHandle(e->handle)&&!ProtectedHandle(e->agent)&&
    e->attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER&&e->count==1&&
    PointerOwner(&Current,e)&&UsbPointerPath(e->handle);
}
static EFI_STATUS PointerBaseline(VOID) {
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    BOOLEAN pointer=Guid(&e->protocol,&PointerGuid),key=Guid(&e->protocol,&InputGuid);
    if((!pointer&&!key)||e->controller!=Plan.console||e->attributes!=8)continue;
    EFI_HANDLE *agent=pointer?&Plan.pointer_agent:&Plan.key_agent;
    /* This board has two xHCI controllers, so the upstream single-host
     * UsbPointerPath()/PointerOwner() gates do not describe it. Record the
     * splitter owners and report the branch verdict; do not refuse. */
    GuLog("pointer: proto=%08x handle=%#llx agent=%#llx usb_path=%u owner=%u\n",
      e->protocol.Data1,(unsigned long long)(UINTN)e->handle,
      (unsigned long long)(UINTN)e->agent,
      (unsigned)(UsbPointerPath(e->handle)?1u:0u),
      (unsigned)(PointerOwner(&Original,e)?1u:0u));
    if(e->count!=1||ProtectedHandle(e->handle)||ProtectedHandle(e->agent))return EFI_UNSUPPORTED;
    if(*agent&&e->agent!=*agent)return EFI_UNSUPPORTED;
    *agent=e->agent;
  }
  return EFI_SUCCESS;
}
/* Freeze which ORIGINAL relationships belong to this board's USB branch
 * while all their handles/paths still exist. Later removal never consults a
 * possibly freed or recycled handle. The USB host's driver/PCI ownership and
 * every GPU/display/application relationship remain mandatory. */
static BOOLEAN UsbConsoleChild(const EDGE *e,const GRAPH *g) {
  BOOLEAN splitter=(Guid(&e->protocol,&PointerGuid)&&Plan.pointer_agent&&e->agent==Plan.pointer_agent)||
    (Guid(&e->protocol,&InputGuid)&&Plan.key_agent&&e->agent==Plan.key_agent);
  return splitter&&e->controller==Plan.console&&e->attributes==8&&e->count==1&&PointerOwner(g,e);
}
static EFI_STATUS UsbBaseline(VOID) {
  memset(OriginalUsb,0,sizeof(OriginalUsb));
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(!Guid(&e->protocol,&UsbHcGuid)||e->attributes!=0x10||e->controller!=e->handle||
       e->count!=1||ProtectedHandle(e->handle)||ProtectedHandle(e->agent))continue;
    /* B550-I exposes two xHCI controllers; record the first and report the
     * rest instead of rejecting the second (upstream assumed exactly one). */
    GuLog("usb host: handle=%#llx agent=%#llx path_pinned=%u\n",
      (unsigned long long)(UINTN)e->handle,(unsigned long long)(UINTN)e->agent,
      (unsigned)(Path(e->handle,UsbHostPath,sizeof(UsbHostPath))?1u:0u));
    if(!Plan.usb_host){Plan.usb_host=e->handle;Plan.usb_agent=e->agent;}
  }
  if(!Plan.usb_host||!Plan.usb_agent)
    GuLog("usb baseline: no host controller found; continuing without USB pin\n");
  UINTN dynamic=0;
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(e->count!=1||ProtectedHandle(e->handle)||ProtectedHandle(e->agent))continue;
    BOOLEAN child=e->handle==Plan.usb_host&&e->agent==Plan.usb_agent&&
      Guid(&e->protocol,&UsbHcGuid)&&e->attributes==8&&!ProtectedHandle(e->controller)&&
      UsbPointerPath(e->controller);
    BOOLEAN physical=UsbPointerPath(e->handle);
    BOOLEAN device=physical&&((e->attributes==0x10&&e->controller==e->handle)||
      (e->attributes==8&&!ProtectedHandle(e->controller)&&UsbPointerPath(e->controller))||
      UsbConsoleChild(e,&Original));
    if(child||device){OriginalUsb[i]=TRUE;dynamic++;}
  }
  GuLog("USB baseline: host=%#llx bus_agent=%#llx dynamic_edges=%zu; host ownership pinned\n",
    (unsigned long long)(UINTN)Plan.usb_host,(unsigned long long)(UINTN)Plan.usb_agent,(size_t)dynamic);
  return EFI_SUCCESS;
}
static BOOLEAN UsbRemoved(UINTN i) {
  if(!OriginalUsb[i])return FALSE;
  const EDGE *e=&Original.edges[i];
  for(UINTN j=0;j<Current.count;j++) {
    const EDGE *c=&Current.edges[j];
    if(c->handle==e->handle&&c->agent==e->agent&&c->controller==e->controller&&Guid(&c->protocol,&e->protocol))return FALSE;
  }
  return TRUE;
}
static BOOLEAN Expected(UINTN stage,BOOLEAN claimed) {
  UINTN own=0;
  for(UINTN i=0;i<Original.count;i++) {
    if(Removed(&Original.edges[i],stage))continue;
    UINTN matches=0;
    for(UINTN j=0;j<Current.count;j++)if(SameEdge(&Original.edges[i],&Current.edges[j]))matches++;
    if(matches!=1&&!(matches==0&&UsbRemoved(i)))return FALSE;
  }
  for(UINTN j=0;j<Current.count;j++) {
    const EDGE *e=&Current.edges[j];
    if(OwnEdge(e)){own++;continue;}
    if(!KnownEdge(e,stage)&&ProtectedEdge(e)&&!PointerAddition(e))return FALSE;
    /* Even unrelated new relationships must not be duplicated. */
    for(UINTN k=j+1;k<Current.count;k++)if(SameEdge(e,&Current.edges[k]))return FALSE;
  }
  return own==(claimed?1u:0u);
}
/* v0.5.1 diagnostics: GuLog faults once the USB log is closed, so log only while it is open. */
#define NOTE(...) do{if(GuBootLogOpen())GuLog(__VA_ARGS__);}while(0)
static VOID NoteEdge(const char *kind,const EDGE *e) {
  NOTE("GPU graph %s: handle=%#llx agent=%#llx controller=%#llx protocol=%#x attributes=%#x count=%u\n",kind,
    (unsigned long long)(UINTN)e->handle,(unsigned long long)(UINTN)e->agent,(unsigned long long)(UINTN)e->controller,
    e->protocol.Data1,e->attributes,e->count);
}
/* Name the first stage-3 edge that is missing or unexpected after a failed Expected(). */
static VOID GraphDiff(BOOLEAN claimed) {
  for(UINTN i=0;i<Original.count;i++) {
    if(Removed(&Original.edges[i],3))continue;
    UINTN matches=0;
    for(UINTN j=0;j<Current.count;j++)if(SameEdge(&Original.edges[i],&Current.edges[j]))matches++;
    if(matches!=1&&!(matches==0&&UsbRemoved(i))){NoteEdge(matches?"duplicate":"missing",&Original.edges[i]);return;}
  }
  for(UINTN j=0;j<Current.count;j++) {
    const EDGE *e=&Current.edges[j];
    if(!KnownEdge(e,3)&&!(claimed&&OwnEdge(e))&&ProtectedEdge(e)&&!PointerAddition(e)){NoteEdge("unexpected",e);return;}
    for(UINTN k=j+1;k<Current.count;k++)if(SameEdge(e,&Current.edges[k])){NoteEdge("duplicate",e);return;}
  }
}
static VOID ReportAdditions(VOID) {
  UINTN added=0,removed=0;
  for(UINTN i=0;i<Original.count;i++)if(UsbRemoved(i))removed++;
  if(removed!=Plan.reported_removals&&GuBootLogOpen()) {
    NOTE("GPU graph: %zu original USB relationships absent; host/GPU ownership and console callbacks unchanged\n",(size_t)removed);
    Plan.reported_removals=removed;
  }
  for(UINTN j=0;j<Current.count;j++)
    if(!OwnEdge(&Current.edges[j])&&!KnownEdge(&Current.edges[j],3))added++;
  if(added&&added!=Plan.reported_additions&&GuBootLogOpen()) {
    NOTE("GPU graph: accepted %zu new edges (unrelated or verified USB pointer); GPU ownership unchanged\n",(size_t)added);
    for(UINTN j=0,shown=0;j<Current.count&&shown<8;j++)
      if(!OwnEdge(&Current.edges[j])&&!KnownEdge(&Current.edges[j],3)){NoteEdge("new-unrelated",&Current.edges[j]);shown++;}
    Plan.reported_additions=added;
  }
}
static EFI_STATUS Driver(UINTN which) {
  EFI_HANDLE a=which==0?Plan.out_agent:which==1?Plan.gfx_agent:Plan.nv_agent;
  EFI_DRIVER_BINDING_PROTOCOL *d=NULL;EFI_LOADED_IMAGE_PROTOCOL *im=NULL;
  EFI_STATUS s=Interface(a,&BindingGuid,(VOID **)&d);
  if(s!=EFI_SUCCESS||!d->Stop||d->DriverBindingHandle!=a||!d->ImageHandle)return EFI_UNSUPPORTED;
  s=Interface(d->ImageHandle,&ImageGuid,(VOID **)&im);if(s!=EFI_SUCCESS)return s;
  /* Stage55 observed all three owners: ConSplitter (45,056B, Stop+0x2390),
   * GraphicsConsole (36,864B), nvgop-ad1xx (162,496B).
   * Metadata is NOT code authentication; active detach remains unproven. */
  if(!im->ImageBase||im->ImageCodeType!=EfiBootServicesCode)return EFI_UNSUPPORTED;
  UINTN stopoff=(UINTN)d->Stop-(UINTN)im->ImageBase;
  if((which==0&&(im->ImageSize!=45056||d->Version!=0x10u||
       stopoff!=0x2390||!ImagePath(im,OutFile,sizeof(OutFile)))) ||
     (which==1&&(im->ImageSize!=36864||d->Version!=0x10u||!ImagePath(im,GfxFile,sizeof(GfxFile)))) ||
     (which==2&&(im->ImageSize!=162496||d->Version!=0x70011u||!ImagePath(im,NvFile,sizeof(NvFile))))) {
    GuLog("Stage22: measured owner metadata mismatch; driver release refused\n");
    return EFI_SECURITY_VIOLATION;
  }
#if defined(GU_ENABLE_RISKY_ACTIVE)
  /* THIS IS NOT A CRYPTOGRAPHIC OWNER PIN. These AORUS sizes/versions and
   * exact UEFI paths were captured from this single machine, but firmware
   * may change them. The full image bytes are NOT available for hashing.
   * As a deliberate, opt-in experimental risk we accept only these exact
   * observed identities; no donor code hash is consulted for authorization. */
  if(which>2)return EFI_SECURITY_VIOLATION;
  GuLog("STAGE56 EXPERIMENTAL: driver[%u] metadata matched, CODE HASH UNVERIFIED\n",(unsigned)which);
  return EFI_SUCCESS;
#endif
  /* Structural match is not code authentication. Donor image fingerprints
   * below remain untrusted for AORUS and always fail closed. */
  UINTN size=which==0?24576:which==1?14816:162016;
  UINTN stop=which==0?0x126c:which==1?0xa38:0x6ec;
  UINTN code=which==2?0x280:0x2a0,bytes=which==0?19872:which==1?7280:137711;
  const char *hash=which==0?"3ff0f364e3ce0ddd1a1c28b9d66abf49822451fb126e4cbec59daf6be9ed75b8":
    which==1?"1bb42730366614ba5207025300e0b75be2283f7632555569f18cdcc5a0456083":
    "77632902f0ceb0ba2848200f396ab61eacd7910e9cce7c535f65d6cdbe2b6c32";
  UINTN donor_stopoff=((UINTN)d->Stop>=(UINTN)im->ImageBase)?((UINTN)d->Stop-(UINTN)im->ImageBase):0xffffffffu;
  BOOLEAN hm=(code+bytes<=im->ImageSize)&&
    GuHashMatches((UINT8 *)im->ImageBase+code,bytes,hash);
  const UINT8 *refpath=which==0?OutFile:which==1?GfxFile:NvFile;
  UINTN refbytes=which==2?sizeof(NvFile):sizeof(OutFile);
  GuLog("driver[%u] size=%zu(ref %zu) ver=%#x(ref %#x) stopoff=%#zx(ref %#zx) hash_match=%u pathnodes=%u refpath_bytes=%zu\n",
    (unsigned)which,(size_t)im->ImageSize,(size_t)size,
    (unsigned)d->Version,(unsigned)(which==2?0x7000bu:0x10u),
    (size_t)donor_stopoff,(size_t)stop,(unsigned)(hm?1u:0u),
    (unsigned)(im->FilePath?1u:0u),(size_t)refbytes);
  /* Stage 18: no UEFI ownership data from this AORUS PC authenticates all
   * three loaded driver code images. Donor AD104/MSI reference hashes are NOT
   * AORUS driver qualifications. Hard-stop before DisconnectController.
   * Never infer eligibility solely from EFI_OPEN_PROTOCOL_BY_DRIVER edges. */
  if(im->ImageSize!=size || d->Version!=(which==2?0x7000bu:0x10u) ||
     donor_stopoff!=stop || !hm || !im->FilePath) {
    GuLog("driver[%u] reference image fingerprint mismatch; refusing driver release\n",(unsigned)which);
    return EFI_SECURITY_VIOLATION;
  }
  /* Even an accidental match to the unrelated donor reference is insufficient.
   * Only independently captured, reviewed AORUS driver images could make this
   * an admissible driver-release path. There are currently no such captures. */
  GuLog("AORUS UEFI driver image provenance unverified; refusing driver release\n");
  (VOID)code;(VOID)bytes;(VOID)refpath;
  return EFI_SECURITY_VIOLATION;
}
static EFI_STATUS Alive(VOID) {
  EFI_SYSTEM_TABLE *t=Plan.table;VOID *in=NULL,*out=NULL,*err=NULL;
  if(t->ConsoleOutHandle!=Plan.console||t->ConsoleInHandle!=Plan.input||t->StandardErrorHandle!=Plan.error||
     t->ConIn!=Plan.keys||t->ConOut!=Plan.text||t->StdErr!=Plan.err||!t->ConIn||
     (UINTN)t->ConIn->Reset!=Plan.key_reset||(UINTN)t->ConIn->ReadKeyStroke!=Plan.key_read||
     t->ConIn->WaitForKey!=Plan.key_wait)return EFI_NOT_READY;
  EFI_STATUS s=Interface(Plan.input,&InputGuid,&in);
  if(s==EFI_SUCCESS)s=Interface(Plan.console,&TextGuid,&out);
  if(s==EFI_SUCCESS)s=Interface(Plan.error,&TextGuid,&err);
  return s==EFI_SUCCESS&&(in!=Plan.keys||out!=Plan.text||err!=Plan.err)?EFI_NOT_READY:s;
}
/* Walk a single-instance UEFI path with a strict byte/node bound. The End
 * node is part of the validated path and must be the final node. */
static BOOLEAN BoundedPathSize(const EFI_DEVICE_PATH_PROTOCOL *p,UINTN *bytes) {
  if(!p||!bytes)return FALSE;
  const UINT8 *b=(const UINT8 *)p;
  UINTN at=0;
  for(UINTN node=0;node<32&&at+4<=GU_MAX_PATH_BYTES;node++) {
    UINTN len=(UINTN)b[at+2]|((UINTN)b[at+3]<<8);
    if(len<4||len>GU_MAX_PATH_BYTES-at)return FALSE;
    if(b[at]==END_DEVICE_PATH_TYPE) {
      if(b[at+1]!=END_ENTIRE_DEVICE_PATH_SUBTYPE||len!=4)return FALSE;
      *bytes=at+4;
      return TRUE;
    }
    at+=len;
  }
  return FALSE;
}
/* True only for a GOP handle with the entire verified parent PCI device path
 * (or a well-formed descendant). A vendor-id prefix alone is never enough. */
static BOOLEAN UnderController(EFI_HANDLE candidate,EFI_HANDLE root) {
  EFI_DEVICE_PATH_PROTOCOL *cp=NULL,*rp=NULL;
  UINTN cn=0,rn=0;
  if(!candidate||!root||
     Interface(root,&PathGuid,(VOID **)&rp)!=EFI_SUCCESS||
     Interface(candidate,&PathGuid,(VOID **)&cp)!=EFI_SUCCESS||
     !BoundedPathSize(rp,&rn)||!BoundedPathSize(cp,&cn)||
     rn<4||cn<rn||!Equal(rp,cp,rn-4))return FALSE;
  /* If equal, the GOP protocol is installed on the parent controller. */
  if(cn==rn)return Equal(rp,cp,rn);
  /* The child suffix must consist of full device-path nodes and end exactly
   * where BoundedPathSize() says it does. */
  const UINT8 *b=(const UINT8 *)cp;
  UINTN pos=rn-4;
  if(pos+4>=cn)return FALSE; /* at least one full child node + End */
  while(pos+4<=cn) {
    UINTN len=(UINTN)b[pos+2]|((UINTN)b[pos+3]<<8);
    if(len<4||len>cn-pos)return FALSE;
    if(b[pos]==END_DEVICE_PATH_TYPE)return
      b[pos+1]==END_ENTIRE_DEVICE_PATH_SUBTYPE&&len==4&&pos+4==cn;
    pos+=len;
  }
  return FALSE;
}
/* Identify the enabled integrated AMD PCI display root via actual PCI config,
 * not the GOP path's name or a preselected BDF. Any ambiguous roots => refuse.
 * This does not touch Attributes(), DisconnectController(), or GPU MMIO. */
static EFI_STATUS FindAmdDisplayRoot(VOID) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;
  EFI_HANDLE *h=NULL;UINTN n=0,found=0;
  EFI_STATUS s=b->LocateHandleBuffer(ByProtocol,&PciGuid,NULL,&n,&h);
  if(s==EFI_NOT_FOUND)return EFI_SUCCESS;
  if(s!=EFI_SUCCESS)return s;
  if((n&&!h)||n>256)s=EFI_BAD_BUFFER_SIZE;
  for(UINTN i=0;s==EFI_SUCCESS&&i<n;i++) {
    EFI_PCI_IO_PROTOCOL *io=NULL;
    UINT32 id=0,klass=0;
    EFI_STATUS st=Interface(h[i],&PciGuid,(VOID **)&io);
    if(st!=EFI_SUCCESS||!io||!io->Pci.Read){s=EFI_COMPROMISED_DATA;break;}
    st=io->Pci.Read(io,EfiPciIoWidthUint32,0,1,&id);
    if(st!=EFI_SUCCESS){s=st;break;}
    if((id&0xffffu)!=0x1002u)continue;
    st=io->Pci.Read(io,EfiPciIoWidthUint32,8,1,&klass);
    if(st!=EFI_SUCCESS){s=st;break;}
    if((klass>>24)!=0x03u)continue;
    EFI_DEVICE_PATH_PROTOCOL *p=NULL;UINTN bytes=0;
    if(Interface(h[i],&PathGuid,(VOID **)&p)!=EFI_SUCCESS||
       !BoundedPathSize(p,&bytes)||h[i]==Plan.pci||
       Path(h[i],PciPath,sizeof(PciPath))){s=EFI_UNSUPPORTED;break;}
    if(++found>1){s=EFI_UNSUPPORTED;break;}
    Plan.amd_pci=h[i];
  }
  if(h&&b->FreePool(h)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  if(s!=EFI_SUCCESS)Plan.amd_pci=NULL;
  return s;
}
static EFI_STATUS Gops(BOOLEAN first) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;
  EFI_HANDLE *h=NULL;UINTN count=0,nv_count=0,foreign_seen=0;
  EFI_STATUS s=b->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&h);
  if(s==EFI_NOT_FOUND) {
    return Plan.stage>=3&&!Plan.foreign_count?EFI_SUCCESS:EFI_NOT_READY;
  }
  if(s!=EFI_SUCCESS)return s;
  if((count&&!h)||count>32)s=EFI_UNSUPPORTED;
  if(first)Plan.foreign_count=0;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *g=NULL;
    s=Interface(h[i],&GopGuid,(VOID **)&g);
    if(s!=EFI_SUCCESS||!g){s=EFI_COMPROMISED_DATA;break;}
    if(h[i]==Plan.console&&g->Mode&&!g->Mode->FrameBufferBase&&!g->Mode->FrameBufferSize)
      continue; /* virtual console GOP, not a physical framebuffer */
    if(!g->Mode||!g->Mode->Info||
       g->Mode->SizeOfInfo<sizeof(*g->Mode->Info)||
       !g->Mode->FrameBufferBase||!g->Mode->FrameBufferSize){s=EFI_UNSUPPORTED;break;}
    if(GpuChild(h[i])) {
      /* No NVIDIA GOP is admissible once the target controller was released. */
      if(Plan.stage>=3||++nv_count>1){s=EFI_NOT_READY;break;}
      if(first){
        Plan.gop=h[i];Plan.gop_interface=g;
        Plan.mode=*g->Mode;Plan.info=*g->Mode->Info;
      } else if(h[i]!=Plan.gop||g!=Plan.gop_interface||
         !Equal(g->Mode,&Plan.mode,sizeof(Plan.mode))||
         !Equal(g->Mode->Info,&Plan.info,sizeof(Plan.info)))s=EFI_NOT_READY;
      continue;
    }
    if(!Plan.amd_pci||!UnderController(h[i],Plan.amd_pci)){
      NOTE("Stage48: foreign/unknown GOP refused; cannot assume AMD ownership\n");
      s=EFI_SECURITY_VIOLATION;break;
    }
    if(first){
      if(Plan.foreign_count>=GU_MAX_FOREIGN_GOPS){s=EFI_UNSUPPORTED;break;}
      for(UINTN j=0;j<Plan.foreign_count;j++)
        if(Plan.foreign[j].handle==h[i]){s=EFI_COMPROMISED_DATA;break;}
      if(s!=EFI_SUCCESS)break;
      FOREIGN_GOP *f=&Plan.foreign[Plan.foreign_count++];
      f->handle=h[i];f->interface=g;f->mode=*g->Mode;f->info=*g->Mode->Info;
    } else {
      UINTN matches=0;
      for(UINTN j=0;j<Plan.foreign_count;j++){
        const FOREIGN_GOP *f=&Plan.foreign[j];
        if(f->handle!=h[i])continue;
        if(f->interface!=g||!Equal(g->Mode,&f->mode,sizeof(f->mode))||
           !Equal(g->Mode->Info,&f->info,sizeof(f->info))){s=EFI_NOT_READY;break;}
        matches++;
      }
      if(s==EFI_SUCCESS&&matches!=1)s=EFI_NOT_READY;
    }
    foreign_seen++;
  }
  if(h&&b->FreePool(h)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  if(s==EFI_SUCCESS&&Plan.stage<3&&nv_count!=1)s=EFI_NOT_READY;
  if(s==EFI_SUCCESS&&Plan.stage>=3&&nv_count!=0)s=EFI_NOT_READY;
  if(s==EFI_SUCCESS&&!first&&foreign_seen!=Plan.foreign_count)s=EFI_NOT_READY;
  return s;
}
static EFI_STATUS ReadConfig(UINT32 c[64]) {
  EFI_PCI_IO_PROTOCOL *io=NULL;UINTN seg,bus,dev,fun;
  EFI_STATUS s=Interface(Plan.pci,&PciGuid,(VOID **)&io);
  if(s!=EFI_SUCCESS||io!=Gu.pci||!io->Pci.Read||!io->GetLocation)return EFI_NOT_READY;
  s=io->GetLocation(io,&seg,&bus,&dev,&fun);
  if(s!=EFI_SUCCESS||seg||bus!=1||dev||fun)return EFI_UNSUPPORTED;
  s=io->Pci.Read(io,EfiPciIoWidthUint32,0,64,c);
  if(s==EFI_SUCCESS&&(!GuTargetConfig(c,seg,bus,dev,fun)||(c[1]&4)))s=EFI_UNSUPPORTED;
  return s;
}
/* RUN0002 did not identify PCI drift: do not assume it caused the stop.
 * Retain v0.5.0's narrow rule: only preexisting CED/URD may clear after FLR. */
static BOOLEAN ConfigStable(const UINT32 c[64]) {
  for(UINTN i=0;i<64;i++) {
    UINT32 drift=c[i]^Plan.config[i];
    if(!drift)continue;
    BOOLEAN allowed=Gu.flrs==1&&i==0x80/4&&Plan.config[0x78/4]==0x0012b410&&
      !(c[i]&~Plan.config[i])&&!(drift&~0x00090000u);
    if(!allowed) {
      NOTE("PCI config drift refused: offset=%#x captured=%#x now=%#x bits=%#x flrs=%u\n",
        (unsigned)(i*4),Plan.config[i],c[i],drift,Gu.flrs);
      return FALSE;
    }
    NOTE("PCI status drift tolerated: offset=%#x captured=%#x now=%#x bits=%#x flrs=%u\n",
      (unsigned)(i*4),Plan.config[i],c[i],drift,Gu.flrs);
  }
  return TRUE;
}
static EFI_STATUS Bar(VOID) {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d=NULL;UINT64 support=0;
  if(!Gu.pci->GetBarAttributes)return EFI_UNSUPPORTED;
  EFI_STATUS s=Gu.pci->GetBarAttributes(Gu.pci,0,&support,(VOID **)&d);
  if(s==EFI_SUCCESS&&(!d||d->Desc!=0x8a||d->Len!=43||d->ResType||
     d->AddrRangeMin!=GU_BAR0_BASE||d->AddrRangeMax!=0xdeffffff||
     d->AddrTranslationOffset||d->AddrLen!=0x1000000))s=EFI_UNSUPPORTED;
  if(d&&Plan.table->BootServices->FreePool(d)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  return s;
}
/* Stage56: strict three-owner plan. This helper performs no UEFI mutations.
 * Target only the NVIDIA GOP-child controller, then NVIDIA PCI root.
 * Avoid any image-wide or global ConSplitter disconnect. */
typedef struct {EFI_HANDLE controller,driver,child;} GU_DETACH_STEP;
static EFI_STATUS ThreeOwnerPlan(GU_DETACH_STEP steps[3]) {
  if(!steps)return EFI_INVALID_PARAMETER;
  memset(steps,0,3*sizeof(*steps));
  if(!Plan.pci||!Plan.gop||!Plan.nv_agent||!Plan.gfx_agent||
     !Plan.out_agent||!Plan.gop_text||!Plan.gop_gfx||
     Plan.nv_agent==Plan.gfx_agent||Plan.nv_agent==Plan.out_agent||
     Plan.out_agent==Plan.gfx_agent||
     Plan.gop_text!=Plan.gop||Plan.gop_gfx!=Plan.gop||
     !GpuChild(Plan.gop)||Plan.gop==Plan.pci)return EFI_SECURITY_VIOLATION;
  /* Stage58: explicit, single-NVIDIA-GOP, NO-iGPU experiment. This DOES NOT
   * demonstrate Windows/console recovery once NVIDIA GOP is detached.
   * Never generalize this exception to configurations with a second GPU,
   * a firmware AMD PCI root without an AMD GOP, or an unknown GOP. */
#if defined(GU_EXPERIMENT_HEADLESS_NVIDIA_ONLY)
  if(Plan.amd_pci||Plan.foreign_count){
    GuLog("Stage58 REFUSE: headless mode requires AMD PCI root=0 and foreign physical GOP=0\n");
    return EFI_SECURITY_VIOLATION;
  }
  GuLog("Stage58 NO-AMD EXPERIMENT: single NVIDIA physical GOP at preflight, NO FALLBACK DISPLAY, NO ROLLBACK\n");
#else
  if(!Plan.amd_pci||!Plan.foreign_count||Plan.amd_pci==Plan.pci||
     Plan.amd_pci==Plan.gop)return EFI_SECURITY_VIOLATION;
#endif
  for(UINTN i=0;i<Plan.foreign_count;i++){
    EFI_HANDLE h=Plan.foreign[i].handle;
    if(!h||h==Plan.gop||h==Plan.pci||h==Plan.amd_pci||
       h==Plan.nv_agent||h==Plan.gfx_agent||h==Plan.out_agent)
      return EFI_SECURITY_VIOLATION;
  }
  steps[0]=(GU_DETACH_STEP){Plan.gop_text,Plan.out_agent,NULL};
  steps[1]=(GU_DETACH_STEP){Plan.gop_gfx,Plan.gfx_agent,NULL};
  steps[2]=(GU_DETACH_STEP){Plan.pci,Plan.nv_agent,NULL};
  return EFI_SUCCESS;
}

/* Stage51: conservative, READ-ONLY policy for assessing fallback after a
 * hypothetical two-owner disconnect sequence. A successful ConnectController
 * call never implies identical recovery of the prior UEFI owner graph.
 * This pure predicate performs NO UEFI calls and grants NO detach permission.
 * It is intentionally not used to authorize physical release. */
typedef struct {
  UINT32 issued_mask;      /* bit 0: GraphicsConsole, bit 1: NVIDIA GOP */
  UINT32 detached_mask;    /* observed loss of an original owning edge */
  BOOLEAN reconnect_called;
  BOOLEAN original_graph_unchanged;
  BOOLEAN amd_graph_unchanged;
  BOOLEAN pci_config_unchanged;
  BOOLEAN console_callbacks_unchanged;
  BOOLEAN dma_inactive;
  BOOLEAN fwsec_inactive;
  BOOLEAN no_unknown_owner;
} GU_GOP_RECOVERY_EVIDENCE;
/* Policy is not invoked by the disabled hardware path. Mark it explicitly
 * as intentionally unreferenced in the production compilation; host tests
 * call it directly. Do not count this as a wired recovery implementation. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static BOOLEAN GopStockFallbackAllowed(const GU_GOP_RECOVERY_EVIDENCE *e) {
  if(!e)return FALSE;
  /* A callback can modify device state even when it returns an error or
   * reports success without an observable graph change. No automatic retry,
   * rollback, or Windows handoff after any attempted callback. */
  if(e->issued_mask||e->detached_mask||e->reconnect_called)return FALSE;
  return e->original_graph_unchanged&&e->amd_graph_unchanged&&
    e->pci_config_unchanged&&e->console_callbacks_unchanged&&
    e->dma_inactive&&e->fwsec_inactive&&e->no_unknown_owner;
}

#if defined(GU_STAGE55_READONLY)
/* Stage55: observations only. Print the actual IMAGE identity behind every
 * BY_DRIVER agent; handle pointers alone say nothing about a driver's provenance.
 * We do not call Stop(), DisconnectController(), ConnectController(), DMA or FLR. */
static VOID Stage55Owner(const char *role,EFI_HANDLE agent) {
  EFI_DRIVER_BINDING_PROTOCOL *binding=NULL;
  EFI_LOADED_IMAGE_PROTOCOL *image=NULL;
  EFI_STATUS bs=agent?Interface(agent,&BindingGuid,(VOID **)&binding):EFI_NOT_FOUND;
  GuLog("Stage55 OWNER %s: agent=%#llx binding_status=%#llx\n",role,
    (unsigned long long)(UINTN)agent,(unsigned long long)bs);
  if(bs!=EFI_SUCCESS||!binding)return;
  EFI_STATUS is=binding->ImageHandle?
    Interface(binding->ImageHandle,&ImageGuid,(VOID **)&image):EFI_NOT_FOUND;
  GuLog("Stage55 BINDING %s: version=%#x binding_handle=%#llx image_handle=%#llx image_status=%#llx\n",
    role,(unsigned)binding->Version,
    (unsigned long long)(UINTN)binding->DriverBindingHandle,
    (unsigned long long)(UINTN)binding->ImageHandle,(unsigned long long)is);
  if(is!=EFI_SUCCESS||!image)return;
  UINTN stopoff=(UINTN)-1;
  if(image->ImageBase && binding->Stop &&
     (UINTN)binding->Stop>=(UINTN)image->ImageBase &&
     (UINTN)binding->Stop-(UINTN)image->ImageBase<image->ImageSize)
    stopoff=(UINTN)binding->Stop-(UINTN)image->ImageBase;
  GuLog("Stage55 IMAGE %s: image_base=%#llx size=%zu code_type=%u data_type=%u stop_offset=%#llx\n",
    role,(unsigned long long)(UINTN)image->ImageBase,(size_t)image->ImageSize,
    (unsigned)image->ImageCodeType,(unsigned)image->ImageDataType,
    (unsigned long long)stopoff);
  if(!image->FilePath){GuLog("Stage55 PATH %s: missing\n",role);return;}
  /* Device path node header is inspected only within a small 128-byte cap.
   * EndEntire is mandatory; raw bytes are logged instead of claiming identity
   * from handle numbers or a guessed driver name. */
  const UINT8 *bytes=(const UINT8 *)image->FilePath;
  UINTN pos=0;
  BOOLEAN valid=FALSE;
  for(UINTN nodes=0;nodes<16 && pos+4<=128;nodes++){
    UINTN len=(UINTN)bytes[pos+2]|((UINTN)bytes[pos+3]<<8);
    if(len<4||len>128-pos)break;
    if(bytes[pos]==END_DEVICE_PATH_TYPE){
      valid=(bytes[pos+1]==END_ENTIRE_DEVICE_PATH_SUBTYPE && len==4);
      pos+=len;break;
    }
    pos+=len;
  }
  if(!valid){GuLog("Stage55 PATH %s: invalid-or-too-long (<=128 bytes), path not trusted\n",role);return;}
  char hex[257];static const char digits[]="0123456789abcdef";
  for(UINTN i=0;i<pos;i++){
    hex[i*2]=digits[bytes[i]>>4];hex[i*2+1]=digits[bytes[i]&15];
  }
  hex[2*pos]=0;
  GuLog("Stage55 PATH %s: length=%zu hex=%s\n",role,(size_t)pos,hex);
}
#endif

static EFI_STATUS Consumers(VOID) {
  UINTN pci=0,child=0,text=0,virtual=0,gfx=0;
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    /* Dump every relationship that touches our two protected handles so the
     * real firmware topology can be read off the log. proto d1 identifies the
     * protocol: PCI_IO=4cf5b200 DP=09576e91 TEXT=387477c1 GOP=9042a9de */
    if(e->handle==Plan.pci||GpuChild(e->handle))
      GuLog("edge tgt=%s proto=%08x attr=%#x ctrl=%s agent=%#llx n=%u\n",
        e->handle==Plan.pci?"PCI":"GOP",e->protocol.Data1,(unsigned)e->attributes,
        e->controller==Plan.pci?"PCI":GpuChild(e->controller)?"GOP":
        e->controller==Plan.console?"CON":e->controller==Plan.self?"SELF":"other",
        (unsigned long long)(UINTN)e->agent,(unsigned)e->count);
    if(e->handle==Plan.pci) {
      if(e->count!=1)return EFI_ACCESS_DENIED;
      if(Guid(&e->protocol,&PciGuid)&&e->attributes==0x10&&e->controller==Plan.pci){Plan.nv_agent=e->agent;pci++;}
      else if(Guid(&e->protocol,&PathGuid)&&e->attributes==8&&GpuChild(e->controller)){child++;}
      else GuLog("consumers: extra PCI-handle edge proto=%08x attr=%#x ctrl_is_pci=%u\n",
        e->protocol.Data1,(unsigned)e->attributes,(unsigned)(e->controller==Plan.pci));
    } else if(GpuChild(e->handle)) {
      if(e->count!=1)return EFI_ACCESS_DENIED;
      if(Guid(&e->protocol,&TextGuid)&&e->attributes==0x10&&GpuChild(e->controller)&&!Plan.out_agent){Plan.out_agent=e->agent;Plan.gop_text=e->handle;text++;}
      else if(Guid(&e->protocol,&TextGuid)&&e->attributes==8&&e->controller==Plan.console){virtual++;}
      else if(Guid(&e->protocol,&GopGuid)&&e->attributes==0x10&&GpuChild(e->controller)&&!Plan.gfx_agent){Plan.gfx_agent=e->agent;Plan.gop_gfx=e->handle;gfx++;}
      else GuLog("consumers: extra GOP-handle edge proto=%08x attr=%#x ctrl_is_gop=%u\n",
        e->protocol.Data1,(unsigned)e->attributes,(unsigned)(e->controller==Plan.gop));
    }
  }
  GuLog("Consumers counts: pci=%zu child=%zu text=%zu virtual=%zu gfx=%zu nv=%#llx out=%#llx gfx_agent=%#llx console=%#llx\n",
    (size_t)pci,(size_t)child,(size_t)text,(size_t)virtual,(size_t)gfx,
    (unsigned long long)(UINTN)Plan.nv_agent,(unsigned long long)(UINTN)Plan.out_agent,
    (unsigned long long)(UINTN)Plan.gfx_agent,(unsigned long long)(UINTN)Plan.console);
  GuLog("Consumers owners: gop_protocol=%#llx text_owner=%#llx gfx_owner=%#llx\n",
    (unsigned long long)(UINTN)Plan.gop,(unsigned long long)(UINTN)Plan.gop_text,
    (unsigned long long)(UINTN)Plan.gop_gfx);
#if defined(GU_STAGE55_READONLY)
  GuLog("STAGE55 DIAGNOSTIC: driver identities; NO release attempted\n");
  Stage55Owner("TEXT",Plan.out_agent);
  Stage55Owner("GRAPHICS",Plan.gfx_agent);
  Stage55Owner("NVIDIA",Plan.nv_agent);
#endif
  /* text/virtual stay 0 on firmware whose ConOut splitter never opens TextOut on
   * the GOP handle. Only the three owning edges are mandatory. */
  if(pci!=1||child<1||gfx!=1||text>1||virtual>1||!Plan.nv_agent||!Plan.gfx_agent||
     (Plan.out_agent&&(Plan.nv_agent==Plan.out_agent||Plan.out_agent==Plan.gfx_agent))||
     Plan.gfx_agent==Plan.nv_agent)return EFI_ACCESS_DENIED;
  for(UINTN i=0;i<Original.count;i++) {
    const EDGE *e=&Original.edges[i];
    if(e->handle==Plan.pci&&e->agent!=Plan.nv_agent)return EFI_ACCESS_DENIED;
    if(GpuChild(e->handle)&&Guid(&e->protocol,&TextGuid)&&Plan.out_agent&&e->agent!=Plan.out_agent)return EFI_ACCESS_DENIED;
  }
  /* Stage56: require EXACTLY one ConSplitter TEXT opener/virtual edge,
   * one GraphicsConsole GOP opener, and one NVIDIA root owner. */
  if(pci!=1||text!=1||virtual!=1||gfx!=1||child!=3)return EFI_SECURITY_VIOLATION;
  GU_DETACH_STEP plan[3];
  EFI_STATUS gate=ThreeOwnerPlan(plan);
  if(gate!=EFI_SUCCESS){
    GuLog("Stage56: exact three-owner GOP plan refused; no detach\n");
    return gate;
  }
  for(UINTN i=0;i<3;i++) {
    EFI_STATUS s=Driver(i);
    if(s!=EFI_SUCCESS)return s;
  }
  return EFI_SUCCESS;
}
EFI_STATUS GuMsiPreflight(EFI_SYSTEM_TABLE *t,EFI_HANDLE self,EFI_HANDLE pci) {
  if(Plan.mutated||Plan.ready)return EFI_ALREADY_STARTED;
  memset(&Plan,0,sizeof(Plan));Plan.table=t;Plan.self=self;Plan.pci=pci;
  if(!t||!t->BootServices||!self||!pci||Gu.handle!=pci||Gu.image!=self||Gu.bs!=t->BootServices||
     !Gu.profile_verified||!Gu.prom_verified||Gu.started||Gu.owned||Gu.armed)return EFI_INVALID_PARAMETER;
  EFI_BOOT_SERVICES *b=t->BootServices;
  if(!b->DisconnectController||!b->OpenProtocol||!b->CloseProtocol||!b->RaiseTPL||!b->RestoreTPL||
     !b->ProtocolsPerHandle||!b->OpenProtocolInformation||!b->LocateHandleBuffer||!b->HandleProtocol)return EFI_UNSUPPORTED;
  Plan.console=t->ConsoleOutHandle;Plan.input=t->ConsoleInHandle;Plan.error=t->StandardErrorHandle;
  Plan.text=t->ConOut;Plan.err=t->StdErr;Plan.keys=t->ConIn;
  if(!Plan.console||Plan.console!=Plan.input||Plan.console!=Plan.error||!Plan.text||!Plan.err||!Plan.keys||
     !Plan.keys->ReadKeyStroke||!Plan.keys->Reset||!Plan.keys->WaitForKey)return EFI_UNSUPPORTED;
  Plan.key_reset=(UINTN)Plan.keys->Reset;Plan.key_read=(UINTN)Plan.keys->ReadKeyStroke;Plan.key_wait=Plan.keys->WaitForKey;
  if(!Path(pci,PciPath,sizeof(PciPath))||!GuAd104ColdEntry())return EFI_NOT_READY;
  EFI_STATUS s=FindAmdDisplayRoot();
  GuLog("preflight[0] AMD PCI display root=%#llx status=%#llx\n",
    (unsigned long long)(UINTN)Plan.amd_pci,(unsigned long long)s);
  if(s==EFI_SUCCESS)s=Gops(TRUE);
  GuLog("preflight[1] Gops=%#llx\n",(unsigned long long)s);
  if(s==EFI_SUCCESS){s=Alive();GuLog("preflight[2] Alive=%#llx\n",(unsigned long long)s);}
  if(s==EFI_SUCCESS){s=Bar();GuLog("preflight[3] Bar=%#llx\n",(unsigned long long)s);}
  if(s==EFI_SUCCESS){s=ReadConfig(Plan.config);GuLog("preflight[4] ReadConfig=%#llx c1=%#x c4=%#x\n",(unsigned long long)s,Plan.config[1],Plan.config[4]);}
  if(s==EFI_SUCCESS){
    s=Gu.pci->Attributes?Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&Plan.attributes):EFI_UNSUPPORTED;
    GuLog("preflight[5] Attributes=%#llx val=%#llx\n",(unsigned long long)s,(unsigned long long)Plan.attributes);
  }
  BOOLEAN firmware_extra_io=FALSE;
  if(s==EFI_SUCCESS && !GuPciDecodeAttributesCompatible((UINT16)Plan.config[1],
       Plan.attributes,&firmware_extra_io)) {
    GuLog("preflight[5b] PCI command/UEFI attrs REFUSED: command=%#x attrs=%#llx\n",
       (unsigned)(Plan.config[1]&0xffffU),(unsigned long long)Plan.attributes);
    s=EFI_NOT_READY;
  } else if(s==EFI_SUCCESS && firmware_extra_io) {
    GuLog("preflight[5b] verified extra firmware IO attribute: PCI IO=off MEM=on BME=off; attrs=%#llx\n",
       (unsigned long long)Plan.attributes);
  }
  if(s==EFI_SUCCESS){s=Snapshot(&Original);GuLog("preflight[6] Snapshot=%#llx edges=%zu\n",(unsigned long long)s,(size_t)Original.count);}
  if(s==EFI_SUCCESS){s=Consumers();GuLog("preflight[7] Consumers=%#llx\n",(unsigned long long)s);}
  if(s==EFI_SUCCESS){s=PointerBaseline();GuLog("preflight[8] PointerBaseline=%#llx\n",(unsigned long long)s);}
  if(s==EFI_SUCCESS){s=UsbBaseline();GuLog("preflight[9] UsbBaseline=%#llx\n",(unsigned long long)s);}
  GuLog("GPU preflight: status=%#llx graph=%zu code_profiles=3\n",(unsigned long long)s,(size_t)Original.count);
  if(s==EFI_SUCCESS)GuLog("GPU endpoints: pci=%#llx gop=%#llx console=%#llx pointer_agent=%#llx\n",
    (unsigned long long)(UINTN)Plan.pci,(unsigned long long)(UINTN)Plan.gop,
    (unsigned long long)(UINTN)Plan.console,(unsigned long long)(UINTN)Plan.pointer_agent);
  if(s==EFI_SUCCESS)Plan.ready=TRUE;return s;
}
#if defined(GU_ENABLE_RISKY_ACTIVE) && !defined(GU_STAGE55_READONLY) /* Stage55 never has claim/write path */
static BOOLEAN DecodeMask(const UINT32 c[64],UINT64 *mask) {
  *mask=0;UINT32 missing=Plan.config[1]&~c[1]&3;
  if((Plan.config[1]&6)!=2||(c[1]&4))return FALSE;
  for(UINTN i=0;i<64;i++)if((c[i]|(i==1?missing:0))!=Plan.config[i])return FALSE;
  *mask=(missing&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(missing&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);return TRUE;
}
static EFI_STATUS ClaimRestoreTwoOwner(VOID) {
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;EFI_PCI_IO_PROTOCOL *io=NULL;
  UINT32 c[64];UINT64 mask=0,attrs=0,supported=0;
  EFI_STATUS s=ReadConfig(c);if(s==EFI_SUCCESS&&!DecodeMask(c,&mask))s=EFI_NOT_READY;
  GuLog("GPU decode restore intent: status=%#llx IO_MEM_mask=%#llx\n",(unsigned long long)s,(unsigned long long)mask);
  EFI_TPL old=b->RaiseTPL(TPL_NOTIFY);
  if(s==EFI_SUCCESS)s=ReadConfig(c);
  if(s==EFI_SUCCESS&&!DecodeMask(c,&mask))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=Snapshot(&Current);
  if(s==EFI_SUCCESS&&!Expected(3,FALSE)){GuLog("Stage22 claim restore pre-graph drifted; aborting\n");s=EFI_NOT_READY;}
  if(s==EFI_SUCCESS)s=b->OpenProtocol(Plan.pci,&PciGuid,(VOID **)&io,Plan.self,NULL,EFI_OPEN_PROTOCOL_EXCLUSIVE);
  if(s==EFI_SUCCESS){Plan.claimed=TRUE;Gu.owned=TRUE;if(io!=Gu.pci)s=EFI_COMPROMISED_DATA;}
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&attrs);
  if(s==EFI_SUCCESS&&attrs!=(Plan.attributes&~mask)){GuLog("Stage22 claim attributes mismatch; aborting\n");s=EFI_NOT_READY;}
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationSupported,0,&supported);
  if(s==EFI_SUCCESS&&(supported&mask)!=mask){GuLog("Stage22 decode mask unsupported; aborting\n");s=EFI_NOT_READY;}
  if(s==EFI_SUCCESS)s=Snapshot(&Current);
  if(s==EFI_SUCCESS&&!Expected(3,TRUE)){GuLog("Stage22 claim graph changed; aborting\n");s=EFI_NOT_READY;}
  if(s==EFI_SUCCESS&&mask)s=io->Attributes(io,EfiPciIoAttributeOperationEnable,mask,NULL);
  b->RestoreTPL(old);
  if(s==EFI_SUCCESS)s=ReadConfig(c);
  if(s==EFI_SUCCESS&&!Equal(c,Plan.config,sizeof(c)))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&attrs);
  if(s==EFI_SUCCESS&&attrs!=Plan.attributes)s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=Bar();
  if(s==EFI_SUCCESS&&!GuAd104ColdEntry())s=EFI_NOT_READY;
  GuLog("GPU exclusive claim/decode result: status=%#llx\n",(unsigned long long)s);return s;
}
#endif /* GU_ENABLE_RISKY_ACTIVE */
/* Live release is isolated behind an explicit, high-risk build switch.
 * Without it, retain the Stage51 refusal and regression behaviour. */
#if defined(GU_ENABLE_RISKY_ACTIVE) && !defined(GU_STAGE55_READONLY)
#include "msi_release_three_owner.inc"
#else
#include "msi_release_refusal.inc"
#endif
#if 0 /* RETIRED_DONOR_DISCONNECT: non-executable reference, DO NOT REENABLE */
  if(!Plan.ready||Plan.mutated||Gu.started||Gu.owned)return EFI_NOT_READY;
  EFI_BOOT_SERVICES *b=Plan.table->BootServices;
  for(UINTN stage=1;stage<=3;stage++) {
    GuLog("GPU driver detach intent: stage=%zu\n",(size_t)stage);
    EFI_STATUS s=Alive();UINT32 c[64];
    if(s==EFI_SUCCESS)s=ReadConfig(c);
    if(s==EFI_SUCCESS&&!Equal(c,Plan.config,sizeof(c)))s=EFI_NOT_READY;
    if(s==EFI_SUCCESS&&!GuAd104ColdEntry())s=EFI_NOT_READY;
    if(s==EFI_SUCCESS)s=Gops(FALSE);
    EFI_TPL old=b->RaiseTPL(TPL_NOTIFY);
    if(s==EFI_SUCCESS)s=Snapshot(&Current);
    if(s==EFI_SUCCESS&&!Expected(stage-1,FALSE)){GuLog("Stage22 pre-graph drift at stage %zu; aborting\n",(size_t)stage);s=EFI_NOT_READY;}
    if(s==EFI_SUCCESS)s=Driver(stage-1);
    b->RestoreTPL(old);
    if(s!=EFI_SUCCESS||Gu.fault)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    /* Record before the callback: an error may follow a partial driver stop. */
    Plan.mutated=TRUE;
    /* The owner edge may sit on a different GPU child than the one Gops()
     * selected, so target the handle recorded in Consumers(). */
    EFI_HANDLE ctrl=stage==3?Plan.pci:(stage==1?(Plan.gop_text?Plan.gop_text:Plan.gop)
      :(Plan.gop_gfx?Plan.gop_gfx:Plan.gop));
    s=b->DisconnectController(ctrl,
      stage==1?Plan.out_agent:stage==2?Plan.gfx_agent:Plan.nv_agent,NULL);
    GuLog("GPU driver detach return: stage=%zu status=%#llx\n",(size_t)stage,(unsigned long long)s);
    if(s!=EFI_SUCCESS||Gu.fault)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    Plan.stage=stage;
    s=Snapshot(&Current);
    if(s==EFI_SUCCESS&&!Expected(stage,FALSE)){GuLog("Stage22 post-graph drift at stage %zu; aborting\n",(size_t)stage);s=EFI_NOT_READY;}
    if(s==EFI_SUCCESS&&Alive()!=EFI_SUCCESS){GuLog("Stage22 console changed at stage %zu; aborting\n",(size_t)stage);s=EFI_NOT_READY;}
    if(s==EFI_SUCCESS&&Gops(FALSE)!=EFI_SUCCESS){GuLog("Stage22 GOP changed at stage %zu; aborting\n",(size_t)stage);s=EFI_NOT_READY;}
    if(s!=EFI_SUCCESS)return s;
  }
  EFI_STATUS s=ClaimRestore();
  if(s==EFI_SUCCESS&&!GuMsiReleased())s=EFI_NOT_READY;
  return s;
#endif /* RETIRED_DONOR_DISCONNECT */
BOOLEAN GuMsiReleased(VOID) {
  UINT32 c[64];UINT64 attrs=0;const char *failed=NULL;
  /* This value becomes Gu.display_check and gates the firmware transaction, so
   * it must describe ownership, not cosmetic graph equality. */
  if(!Plan.ready||Plan.stage!=3||!Plan.claimed||!Gu.owned||Gu.fault)failed="state";
  else if(ReadConfig(c)!=EFI_SUCCESS)failed="config-read";
  else if(Alive()!=EFI_SUCCESS)failed="console";
  else if(Gops(FALSE)!=EFI_SUCCESS)failed="GOP";
  else if(!ConfigStable(c)){NOTE("Stage29: PCI config drift REFUSED\n");failed="config-drift";}
  else if(Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&attrs)!=EFI_SUCCESS||attrs!=Plan.attributes)
    {NOTE("Stage29: PCI attributes changed expected=%#llx actual=%#llx\n",(unsigned long long)Plan.attributes,(unsigned long long)attrs);failed="attributes-drift";}
  else if(Snapshot(&Current)!=EFI_SUCCESS)failed="graph-snapshot";
  else if(!Expected(3,TRUE)){NOTE("Stage29: UEFI graph drift REFUSED\n");GraphDiff(TRUE);failed="graph-drift";}
  if(failed)NOTE("GPU release check failed: %s attributes=%#llx expected=%#llx graph=%zu original=%zu fault=%u flrs=%u\n",
    failed,(unsigned long long)attrs,(unsigned long long)Plan.attributes,(size_t)Current.count,(size_t)Original.count,Gu.fault,Gu.flrs);
  if(!failed)ReportAdditions();
  return !failed;
}
EFI_STATUS GuMsiFinish(VOID) {
  if(!GuSafeToReturn()||!Gu.quiesced||Gu.flrs!=1||!GuMsiReleased()){NOTE("GPU finish refused before close\n");return EFI_NOT_READY;}
  EFI_STATUS s=Plan.table->BootServices->CloseProtocol(Plan.pci,&PciGuid,Plan.self,NULL);
  if(s!=EFI_SUCCESS){NOTE("GPU finish close status=%#llx\n",(unsigned long long)s);return s;}
  Plan.claimed=FALSE;Gu.owned=FALSE;
  s=Snapshot(&Current);if(s==EFI_SUCCESS&&!Expected(3,FALSE)){s=EFI_NOT_READY;GraphDiff(FALSE);}
  if(s!=EFI_SUCCESS)NOTE("GPU finish after close status=%#llx graph=%zu\n",(unsigned long long)s,(size_t)Current.count);
  return s;
}
/* Fail closed if a later implementation forgets to set Plan.mutated
 * while the state already records claimed resources or a partial attempt.
 * GuMsiMutated() is queried by app.c before any stock Windows fallback. */
BOOLEAN GuMsiMutated(VOID){
  return Plan.mutated||Plan.stage!=0||Plan.claimed||Gu.owned||
    Gu.started||Gu.retained||Gu.master_attributes_pending||Gu.dma_count!=0;
}
BOOLEAN GuMsiReadyForWindows(VOID) {
  if(!Plan.ready||Plan.stage!=3||Plan.claimed||Gu.owned||Gu.fault||!Gu.quiesced||Gu.flrs!=1||!GuSafeToReturn())return FALSE;
  UINT32 c[64];
  if(Alive()!=EFI_SUCCESS||Gops(FALSE)!=EFI_SUCCESS||ReadConfig(c)!=EFI_SUCCESS||!ConfigStable(c)||
     Snapshot(&Current)!=EFI_SUCCESS||!Expected(3,FALSE))return FALSE;
  /* Fresh status after USB Close and before StartImage; no log callbacks. */
  UINT32 chip=GuRead32(0),cpu=GuRead32(0x110100),hi=GuRead32(0x1fa828);
  UINT32 ss0=GuRead32(0x82381c),ss1=GuRead32(0x823820),readout=GuRead32(0x823818),plm=GuRead32(0x823804);
  return !Gu.fault&&chip==0x193000a1&&(cpu&0x10)&&cpu!=0xffffffff&&
    (cpu&0xffff0000)!=0xbadf0000&&!(hi>>4)&&ss0==0x88888888&&ss1==8&&
    !(readout&0x07ffffff)&&readout!=0xffffffff&&plm==0xffffffff;
}
