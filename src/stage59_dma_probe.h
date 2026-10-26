/* Stage59 host-memory allocation probe, no Map/no DMA/GOP release. */
#pragma once
#include "port.h"
#ifdef GU_STAGE59_DMA_PROBE_READONLY
static EFI_STATUS GuStage59ProbeAllocate(UINTN Bytes,BOOLEAN *Allocated,BOOLEAN *CleanupOk) {
  EFI_STATUS Status,FreeStatus;
  VOID *Cpu=NULL;
  UINTN Pages=(Bytes+4095U)/4096U;
  *Allocated=FALSE;*CleanupOk=FALSE;
  if(!Gu.pci || !Gu.pci->AllocateBuffer || !Gu.pci->FreeBuffer)return EFI_UNSUPPORTED;
  Status=Gu.pci->AllocateBuffer(Gu.pci,AllocateAnyPages,EfiBootServicesData,Pages,&Cpu,0);
  GuLog("STAGE59 DMA-PROBE AllocateBuffer: request_bytes=%zu pages=%zu EFI_STATUS=%#llx host=%#llx\n",
    (size_t)Bytes,(size_t)Pages,(unsigned long long)Status,(unsigned long long)(UINTN)Cpu);
  if(Status!=EFI_SUCCESS){
    if(Cpu){*Allocated=TRUE;return EFI_COMPROMISED_DATA;}
    return Status;
  }
  if(!Cpu)return EFI_COMPROMISED_DATA;
  *Allocated=TRUE;
  FreeStatus=Gu.pci->FreeBuffer(Gu.pci,Pages,Cpu);
  GuLog("STAGE59 DMA-PROBE FreeBuffer: EFI_STATUS=%#llx\n",(unsigned long long)FreeStatus);
  if(FreeStatus==EFI_SUCCESS)*CleanupOk=TRUE;
  return FreeStatus;
}
#endif
