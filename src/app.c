/* SPDX-License-Identifier: GPL-2.0-only */
#include "port.h"
#include "checks.h"
#include "profile.h"
#include "msi_release.h"
#include "boot_io.h"
#include "compat/compat.h"
#include "vendor/unlock_610.h"

/* This source package is a machine-profile research copy, NOT an EFI unlocker.
 * Disabling this guard alone must still not work: profile.c fails closed
 * until cold register pins and vendor-asset ownership are fully reviewed.
 */
#if defined(GU_AD104_TRANSACTION) && !defined(GU_ENABLE_RISKY_ACTIVE)
#error "AORUS active firmware needs explicit GU_ENABLE_RISKY_ACTIVE; this is unvalidated hardware"
#endif

#include "stage59_dma_probe.h"
#include "stage60_dma_probe.h"
#include "stage61_null_token_probe.h"
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID GlobalGuid={0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0,0xe0,0x98,0x03,0x2b,0x8c}};
static EFI_STATUS Discover(EFI_HANDLE image,EFI_SYSTEM_TABLE *t) {
  EFI_HANDLE *h=NULL,target=NULL;EFI_PCI_IO_PROTOCOL *io=NULL;UINTN count=0,found=0;
  EFI_STATUS s=t->BootServices->LocateHandleBuffer(ByProtocol,&PciGuid,NULL,&count,&h);
  if(s==EFI_SUCCESS&&(!h||!count||count>1024))s=EFI_UNSUPPORTED;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_PCI_IO_PROTOCOL *p=NULL;UINT32 id=0;
    s=t->BootServices->HandleProtocol(h[i],&PciGuid,(VOID **)&p);
    if(s!=EFI_SUCCESS||!p||!p->Pci.Read){s=EFI_UNSUPPORTED;break;}
    s=p->Pci.Read(p,EfiPciIoWidthUint32,0,1,&id);
    if(s==EFI_SUCCESS&&id==GU_BOARD_ID){found++;target=h[i];io=p;}
  }
  if(h)t->BootServices->FreePool(h);
  if(s!=EFI_SUCCESS||found!=1||!io)return EFI_UNSUPPORTED;
  GuInit(t->BootServices,io);Gu.image=image;Gu.handle=target;
  Gu.bar_base=GU_BAR0_BASE;Gu.bar_size=0x1000000;
  UINT32 c[64];UINTN seg,bus,dev,fun;GU_CAPS caps;
  if(!io->GetLocation)return EFI_UNSUPPORTED;
  s=io->GetLocation(io,&seg,&bus,&dev,&fun);
  if(s==EFI_SUCCESS)s=io->Pci.Read(io,EfiPciIoWidthUint32,0,64,c);
  if(s!=EFI_SUCCESS||!GuCheckConfig(c,seg,bus,dev,fun,&caps)||c[2]!=0x030000a1)return EFI_UNSUPPORTED;
  Gu.pm_cap=caps.pm;Gu.pcie_cap=caps.pcie;Gu.pcie_version=caps.pcie_version;Gu.msi_cap=caps.msi;Gu.msix_cap=caps.msix;
  return EFI_SUCCESS;
}
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE image,EFI_SYSTEM_TABLE *t) {
  if(!image||!t||!t->BootServices||!t->RuntimeServices||!t->RuntimeServices->GetVariable)return EFI_INVALID_PARAMETER;
  UINT8 secure=1;UINTN n=1;UINT32 attrs=0;
  EFI_STATUS s=t->RuntimeServices->GetVariable(L"SecureBoot",&GlobalGuid,&attrs,&n,&secure);
  if(s!=EFI_SUCCESS||n!=1||secure)return EFI_SECURITY_VIOLATION;
  /* LoadImage/ReadFile use GuAlloc(), which needs BootServices even before
   * PCI discovery. Keep stock Windows boot independent of the target GPU. */
  GuInit(t->BootServices,NULL);
  s=GuBootOpen(image,t);if(s!=EFI_SUCCESS)return s;
  const char *stage="stage-windows-image";
  BOOLEAN unlock=FALSE, windowsStaged=FALSE;
  /* Stage43: stage the untouched Windows image FIRST. A failed cold SEC2
   * read must never leave pre-mutation fallback without a boot image.
   * A manual skip must not probe SEC2 or require the unlock assets. */
  s=GuBootLoadWindows();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  windowsStaged=TRUE;
  stage="manual-choice";s=GuBootChoose(&unlock);if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  if(!unlock) {
    GuLog("Operator skipped: no GPU transaction, no driver release; stock Windows\n");
    if(Gu.fault||GuBootClose()!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    return GuBootWindows();
  }
  stage="discover";s=Discover(image,t);if(s!=EFI_SUCCESS)goto failed;
  #if defined(GU_STAGE61_NULL_TOKEN_UNMAP_PROBE)
  GuLog("STAGE61 NVIDIA-ONLY NULL TOKEN UNMAP PROBE: NO GOP DISCONNECT / NO GPU DMA / NO FWSEC / NO FLR\n");
  #elif defined(GU_STAGE60_DMA_PROBE_READONLY)
  GuLog("STAGE60 NVIDIA-ONLY DMA MAPPING PROBE: NO GOP DISCONNECT / NO GPU DMA / NO FWSEC / NO FLR\n");
  #elif defined(GU_STAGE59_DMA_PROBE_READONLY)
  GuLog("STAGE59 NVIDIA-ONLY DMA ALLOCATION PROBE: NO GOP DISCONNECT / NO MAP / NO FWSEC / NO GPU DMA / NO FLR\n");
#else
#if defined(GU_STAGE62_ACTIVE)
  GuLog("AD103 STAGE62 NVIDIA-ONLY ACTIVE: NULL identity DMA map guard fixed; NO AMD FALLBACK; NO ROLLBACK; FWSEC/FLR UNTESTED\n");
#else
  GuLog("AD103 STAGE58 NVIDIA-ONLY HEADLESS ACTIVE: SINGLE NVIDIA GOP; NO AMD FALLBACK; NO ROLLBACK; UNTESTED\n");
#endif
#endif
  stage="assets";s=GuBootLoadAssets();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  stage="read-only-cold-profile";s=GuAd104Prepare();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  stage="MSI-preflight";s=GuMsiPreflight(t,image,Gu.handle);if(s!=EFI_SUCCESS||Gu.fault)goto failed;
#if defined(GU_STAGE61_NULL_TOKEN_UNMAP_PROBE)
  {
    BOOLEAN uncertain=FALSE;
    EFI_STATUS probe=GuStage61ProbeDma(56832U,&uncertain);
    GuLog("STAGE61 RESULT: probe=%#llx uncertain=%u firmware_transaction=NOT_STARTED\n",
      (unsigned long long)probe,(unsigned)uncertain);
    if(uncertain) {
      GuLog("STAGE61 uncertain DMA mapping state: HALT; no Windows handoff\n");
      (VOID)GuBootClose();GuBootHalt();return EFI_ABORTED;
    }
    if(GuBootClose()!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    return GuBootWindows();
  }
#elif defined(GU_STAGE60_DMA_PROBE_READONLY)
  {
    BOOLEAN uncertain=FALSE;
    EFI_STATUS probe=GuStage60ProbeDma(56832U,&uncertain);
    GuLog("STAGE60 RESULT: probe=%#llx uncertain=%u firmware_transaction=NOT_STARTED\n",
      (unsigned long long)probe,(unsigned)uncertain);
    if(uncertain) {
      GuLog("STAGE60 uncertain DMA mapping state: HALT; no Windows handoff\n");
      (VOID)GuBootClose();GuBootHalt();return EFI_ABORTED;
    }
    if(GuBootClose()!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    return GuBootWindows();
  }
#elif defined(GU_STAGE59_DMA_PROBE_READONLY)
  /* Read-only with respect to GPU: temporary UEFI host-memory allocation,
   * no PCI Map, no GPU driver disconnect, no firmware transaction or FLR. */
  {
    BOOLEAN allocated=FALSE,cleanup_ok=FALSE;
    EFI_STATUS probe=GuStage59ProbeAllocate(56832U,&allocated,&cleanup_ok);
    GuLog("STAGE59 RESULT: allocate_probe=%#llx allocated=%u cleanup_ok=%u firmware_transaction=NOT_STARTED\n",
      (unsigned long long)probe,(unsigned)allocated,(unsigned)cleanup_ok);
    /* A failed AllocateBuffer made no allocation: normal Windows is safe.
     * If allocation succeeded but release did not, we cannot hand off. */
    if((allocated && !cleanup_ok)||probe==EFI_COMPROMISED_DATA){
      GuLog("STAGE59 uncertain resource state: halt; DO NOT start Windows\n");
      (VOID)GuBootClose();GuBootHalt();return EFI_ABORTED;
    }
    if(GuBootClose()!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    return GuBootWindows();
  }
#elif defined(GU_STAGE55_READONLY)
  /* This build is deliberately not an unlocker. Never proceed to release,
   * DMA, vendor FWSEC operations or FLR, even if all preflight checks PASS. */
  GuLog("STAGE55 READ-ONLY DONE: preflight PASS; disconnect/SEC2/DMA/FLR skipped; ordinary Windows handoff\n");
  if(GuBootClose()!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
  return GuBootWindows();
#else
  GuBootSay(L"\r\nHEADLESS: sole GPU display will be disconnected. Windows recovery UNTESTED.\r\n");
  GuLog("Operator U: one physical attempt; no retries; display callbacks disabled\n");
  if(Gu.fault){s=EFI_DEVICE_ERROR;goto failed;}
  GuBootSilence();
  stage="MSI-release";s=GuMsiRelease();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  if(!GuMsiReleased()||!GuAd104ColdEntry()){s=EFI_NOT_READY;goto failed;}
  Gu.display_check=GuMsiReleased;Gu.display_ok=TRUE;Gu.armed=TRUE;
  UINT64 mask=0xffffffffffffULL;
  struct pci_dev dev={.vendor=0x10de,.device=0x2705u,.dev={.dma_mask=&mask,.coherent_dma_mask=0xfffffffffULL},
    .current_state=PCI_D0,.pm_cap=Gu.pm_cap,.refs=1};
  struct gpu_unlock_610_shared shared={0};
  stage="shared-assets";int result=gpu_unlock_610_load_shared(&dev,&shared);
  if(!result) {stage="firmware-transaction";result=gpu_unlock_610_unlock(&dev,&shared);}
  gpu_unlock_610_release_shared(&shared);
  GuLog("Transaction result=%d safe=%u retained=%u started=%u quiesced=%u flrs=%u DMA_live=%u\n",
    result,GuSafeToReturn(),Gu.retained,Gu.started,Gu.quiesced,Gu.flrs,Gu.dma_count);
  if(result||Gu.fault){
    /* Stage 16 policy: any failed firmware transaction forbids Windows handoff. */
    GuLog("transaction incomplete result=%d fault=%u; refusing Windows handoff\n",result,Gu.fault);
    s=EFI_DEVICE_ERROR;goto failed;
  }
  stage="release-exclusive-claim";s=GuMsiFinish();
  if(s!=EFI_SUCCESS||Gu.fault){
    /* Never convert a failed final driver release into a successful boot. */
    GuLog("finish incomplete status=%#llx; refusing Windows handoff\n",(unsigned long long)s);
    if(s==EFI_SUCCESS)s=EFI_DEVICE_ERROR;
    goto failed;
  }
  /* The final pre-Windows check runs after the log closes: this line is not proof of a Windows start. */
  GuLog("TRANSACTION OK: hardware readbacks passed, DMA released; final pre-Windows check follows unlogged; benchmark still required\n");
  stage="close-USB-log";s=GuBootClose();
  if(s!=EFI_SUCCESS||Gu.fault){
    /* USB logging failure after GPU mutation is not a safe handoff. */
    GuBootHalt();return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
  }
  stage="final-Windows-verification";
  if(!GuMsiReadyForWindows()){
    /* No image start when GPU postconditions are false. Log is already closed. */
    GuBootHalt();return EFI_NOT_READY;
  }
  stage="start-Windows";s=GuBootWindows();
  /* A returned Windows image is not a successful handoff. */
  GuBootHalt();return s==EFI_SUCCESS?EFI_ABORTED:s;
#endif /* !GU_STAGE55_READONLY */
failed:
  if(s==EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  GuLog("STOP: stage=%s status=%#llx mutated=%u retained=%u fault=%u\n",stage,(unsigned long long)s,GuMsiMutated(),Gu.retained,Gu.fault);
  if(GuMsiMutated()||Gu.started||Gu.retained||!GuSafeToReturn()) {
    /* Real containment: the GPU is mid-operation or DMA is live. Do not hand
     * this state to Windows. */
    if(Gu.started||Gu.master_attributes_pending)(VOID)GuClearMaster();
    (VOID)GuBootClose();GuBootHalt();return EFI_ABORTED;
  }
  /* No Windows image was loaded if LoadImage itself failed. Do not try to
   * StartImage on a missing handle. The user's firmware boot manager remains
   * the recovery route in that case. */
  if(!windowsStaged) {
    GuLog("STOP before Windows image staging; no StartImage attempted\n");
    (VOID)GuBootClose();
    return s;
  }
  /* Pre-mutation rejection only. SEC2/asset failure must not block the
   * already loaded stock boot manager, and no GPU write is permitted here. */
  GuLog("STOP before mutation; stock Windows handoff permitted\n");
  (VOID)GuBootClose();
  return GuBootWindows();
}
