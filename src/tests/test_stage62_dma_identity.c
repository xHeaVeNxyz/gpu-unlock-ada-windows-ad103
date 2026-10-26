/* Test real production GuDmaAllocate/GuDmaFree with a low aligned static buffer.
 * Compilers should map this mock exe below 4 GiB; this does not test physical GPU DMA. */
#include "../port.h"
#include "../stage62_dma_identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void GuLog(const char *fmt,...){(void)fmt;}
static EFI_PCI_IO_PROTOCOL io;static EFI_BOOT_SERVICES bs;
static unsigned char backing[131072] __attribute__((aligned(4096)));
static UINT64 bus_bias=0;
static int map_token=0,partial=0,unmap_error=0;
static int nalloc=0,nfree=0,nmap=0,nunmap=0,pool_live=0;
static void ck(int yes, const char *label){if(!yes){fprintf(stderr,"FAIL %s\n",label);exit(1);}printf("PASS %s\n",label);}
static EFI_STATUS EFIAPI pa(EFI_MEMORY_TYPE t,UINTN n,VOID **out){(void)t;*out=calloc(1,n);if(*out)pool_live++;return *out?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;}
static EFI_STATUS EFIAPI pf(VOID *ptr){free(ptr);pool_live--;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI ba(EFI_PCI_IO_PROTOCOL *p,EFI_ALLOCATE_TYPE t,EFI_MEMORY_TYPE m,UINTN pages,VOID **out,UINT64 attr){
 (void)p;(void)t;(void)m;(void)attr; nalloc++;if(pages>32)return EFI_OUT_OF_RESOURCES;*out=backing;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI bf(EFI_PCI_IO_PROTOCOL *p,UINTN pg,VOID *ptr){(void)p;(void)pg;nfree++;return ptr==backing?EFI_SUCCESS:EFI_INVALID_PARAMETER;}
static EFI_STATUS EFIAPI map(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_OPERATION op,VOID *cpu,UINTN *len, EFI_PHYSICAL_ADDRESS *bus, VOID **token){
 (void)p;nmap++;if(op!=EfiPciIoOperationBusMasterCommonBuffer)return EFI_INVALID_PARAMETER;
 if(partial)*len/=2;*bus=(UINT64)(UINTN)cpu+bus_bias;*token=map_token?(VOID *)0x12340:NULL;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI unmap(EFI_PCI_IO_PROTOCOL *p,VOID *token){
 (void)p;nunmap++;if(token!=(map_token?(VOID *)0x12340:NULL))return EFI_INVALID_PARAMETER;
 return unmap_error?EFI_DEVICE_ERROR:EFI_SUCCESS;}
static void setup(void){
 memset(&io,0,sizeof(io));memset(&bs,0,sizeof(bs));
 io.AllocateBuffer=ba;io.FreeBuffer=bf;io.Map=map;io.Unmap=unmap;
 bs.AllocatePool=pa;bs.FreePool=pf;
 GuInit(&bs,&io);Gu.profile_verified=TRUE;Gu.armed=TRUE;Gu.display_ok=TRUE;Gu.owned=TRUE;
 bus_bias=0;map_token=0;partial=0;unmap_error=0;
 nalloc=nfree=nmap=nunmap=pool_live=0;
}
int main(void){
 VOID *cpu=NULL;UINT64 dev=0;EFI_STATUS r;
 ck(Gu62IdentityMappingOk((void *)(UINTN)0x71870000,57344,57344,0x71870000),"identity_allowed_exact_stage61_geometry");
 ck(!Gu62IdentityMappingOk((void *)(UINTN)0x71870000,57344,53248,0x71870000),"identity_partial_rejected");
 ck(!Gu62IdentityMappingOk((void *)(UINTN)0x71870000,57344,57344,0x71871000),"identity_bus_mismatch_rejected");
 ck(!Gu62IdentityMappingOk((void *)(UINTN)0x100000000ULL,4096,4096,0x100000000ULL),"identity_above_4g_rejected");
 ck(!Gu62IdentityMappingOk((void *)(UINTN)0xfffff000ULL,8192,8192,0xfffff000ULL),"identity_crosses_4g_rejected");
 ck(!Gu62IdentityMappingOk((void *)(UINTN)0x71870001,4096,4096,0x71870001),"identity_unaligned_rejected");
 ck(!Gu62IdentityMappingOk(NULL,4096,4096,0),"identity_null_cpu_rejected");
 ck(!Gu62IdentityMappingOk((void *)(UINTN)0x71870000,0,0,0x71870000),"identity_zero_bytes_rejected");
 ck((UINT64)(UINTN)backing<=0xffffffffULL,"production_fixture_below_4g");
 setup();r=GuDmaAllocate(56832,&cpu,&dev);
 ck(r==EFI_SUCCESS && cpu==backing && dev==(UINT64)(UINTN)backing && Gu.dma_count==1 && !Gu.retained && !Gu.fault,"production_identity_null_token_accepted");
 ck(GuDmaFree(cpu,dev)==EFI_SUCCESS && nunmap==1 && nfree==1 && pool_live==0 && !Gu.allocations,"production_identity_unmap_null_and_free");
 setup();bus_bias=4096;r=GuDmaAllocate(4096,&cpu,&dev);
 ck(r==EFI_COMPROMISED_DATA && Gu.retained && Gu.fault && !GuSafeToReturn() && Gu.dma_count==0 && !cpu,"production_nonidentity_null_token_retained");
 ck(!nunmap && !nfree,"production_nonidentity_no_untracked_free");
 /* test fixture teardown: Gu retained means never free a real DMA allocation */
 setup();partial=1;r=GuDmaAllocate(8192,&cpu,&dev);
 ck(r==EFI_COMPROMISED_DATA && Gu.retained && Gu.fault && !cpu,"production_partial_null_token_rejected");
 setup();r=GuDmaAllocate(4096,&cpu,&dev);unmap_error=1;
 ck(r==EFI_SUCCESS && GuDmaFree(cpu,dev)==EFI_DEVICE_ERROR && Gu.retained && Gu.allocations && nfree==0,"production_failed_unmap_retains_buffer");
 puts("STAGE62 real GuDmaAllocate tests PASS; mocked UEFI only");return 0;
}
