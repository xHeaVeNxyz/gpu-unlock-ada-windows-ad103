/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "port.h"
#ifdef GU_STAGE61_NULL_TOKEN_UNMAP_PROBE
/* No GPU writes, driver disconnect, firmware launch, GPU DMA, or FLR.
 * This attempts to release exactly the opaque mapping token returned by
 * EFI_PCI_IO_PROTOCOL.Map, including NULL if an identity map returned it.
 * If Unmap fails or resource state is uncertain, NEVER free/hand off. */
static EFI_STATUS GuStage61ProbeDma(UINTN Bytes,BOOLEAN *Uncertain) {
  EFI_STATUS s,raw,clean;
  VOID *metadata=NULL,*cpu=NULL,*mapping=NULL;
  EFI_PHYSICAL_ADDRESS bus=0;
  UINTN pages=(Bytes+4095U)/4096U,requested=pages*4096U,mapped=requested;
  *Uncertain=FALSE;
  if(!Gu.bs || !Gu.bs->AllocatePool || !Gu.bs->FreePool || !Gu.pci ||
     !Gu.pci->AllocateBuffer || !Gu.pci->FreeBuffer || !Gu.pci->Map || !Gu.pci->Unmap)
    return EFI_UNSUPPORTED;
  s=Gu.bs->AllocatePool(EfiLoaderData,sizeof(GU_DMA),&metadata);
  GuLog("STAGE61 AllocatePool: bytes=%zu status=%#llx ptr=%#llx\n",
    (size_t)sizeof(GU_DMA),(unsigned long long)s,(unsigned long long)(UINTN)metadata);
  if(EFI_ERROR(s)) {if(metadata)*Uncertain=TRUE;return s;}
  if(!metadata) {*Uncertain=TRUE;return EFI_COMPROMISED_DATA;}
  s=Gu.bs->FreePool(metadata);
  GuLog("STAGE61 FreePool: status=%#llx\n",(unsigned long long)s);
  if(EFI_ERROR(s)){*Uncertain=TRUE;return s;}
  s=Gu.pci->AllocateBuffer(Gu.pci,AllocateAnyPages,EfiBootServicesData,pages,&cpu,0);
  GuLog("STAGE61 AllocateBuffer: pages=%zu bytes=%zu status=%#llx host=%#llx\n",
    (size_t)pages,(size_t)Bytes,(unsigned long long)s,(unsigned long long)(UINTN)cpu);
  if(EFI_ERROR(s)){if(cpu)*Uncertain=TRUE;return s;}
  if(!cpu){*Uncertain=TRUE;return EFI_COMPROMISED_DATA;}
  s=Gu.pci->Map(Gu.pci,EfiPciIoOperationBusMasterCommonBuffer,cpu,&mapped,&bus,&mapping);
  GuLog("STAGE61 Map: status=%#llx requested=%zu mapped=%zu host=%#llx dma_bus=%#llx token=%#llx\n",
    (unsigned long long)s,(size_t)requested,(size_t)mapped,(unsigned long long)(UINTN)cpu,
    (unsigned long long)bus,(unsigned long long)(UINTN)mapping);
  raw=s;
  if(EFI_ERROR(s) && !mapping && (bus || mapped!=requested)){
    GuLog("STAGE61 FAIL CLOSED: failed Map with ambiguous output, no cleanup\n");
    *Uncertain=TRUE;return EFI_COMPROMISED_DATA;
  }
  if(!EFI_ERROR(s) && !mapping && (
     bus!=(EFI_PHYSICAL_ADDRESS)(UINTN)cpu || mapped!=requested || !bus || (bus&4095U) ||
     bus>0xffffffffULL || requested-1U>0xffffffffULL-bus)) {
    GuLog("STAGE61 FAIL CLOSED: NULL token without exact 32-bit identity mapping; no cleanup\n");
    *Uncertain=TRUE;return EFI_COMPROMISED_DATA;
  }
  if(!EFI_ERROR(s) || mapping){
    if(!mapping)GuLog("STAGE61 NULL token: calling Unmap(NULL), not assuming safe cleanup\n");
    clean=Gu.pci->Unmap(Gu.pci,mapping);
    GuLog("STAGE61 Unmap: status=%#llx token=%#llx\n",
      (unsigned long long)clean,(unsigned long long)(UINTN)mapping);
    if(EFI_ERROR(clean)){
      GuLog("STAGE61 HALT: Unmap failed, retaining backing buffer\n");
      *Uncertain=TRUE;return clean;
    }
  }
  if(!EFI_ERROR(s) && (mapped!=requested || !bus || (bus&4095U) ||
    bus>0xffffffffULL || requested-1U>0xffffffffULL-bus))raw=EFI_UNSUPPORTED;
  clean=Gu.pci->FreeBuffer(Gu.pci,pages,cpu);
  GuLog("STAGE61 FreeBuffer: status=%#llx\n",(unsigned long long)clean);
  if(EFI_ERROR(clean)){
    GuLog("STAGE61 HALT: FreeBuffer failed, uncertain lifetime\n");
    *Uncertain=TRUE;return clean;
  }
  GuLog("STAGE61 cleanup_complete=1 firmware_transaction=NOT_STARTED\n");
  return raw;
}
#endif
