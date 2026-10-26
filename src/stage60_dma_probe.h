/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "port.h"
#ifdef GU_STAGE60_DMA_PROBE_READONLY
/* Diagnostic of the same host-memory and PciIo.Map operations that the
 * production GuDmaAllocate requires.  No driver disconnect, GPU bus master,
 * MMIO write, firmware launch or FLR.  Map/Unmap may alter UEFI IOMMU state;
 * any ambiguous cleanup fails closed rather than starting Windows. */
static EFI_STATUS GuStage60ProbeDma(UINTN Bytes,BOOLEAN *Uncertain) {
  EFI_STATUS s,raw,clean;
  VOID *metadata=NULL,*cpu=NULL,*mapping=NULL;
  EFI_PHYSICAL_ADDRESS bus=0;
  UINTN pages=(Bytes+4095U)/4096U,requested=pages*4096U,mapped=requested;
  *Uncertain=FALSE;
  if(!Gu.bs || !Gu.bs->AllocatePool || !Gu.bs->FreePool || !Gu.pci ||
     !Gu.pci->AllocateBuffer || !Gu.pci->FreeBuffer || !Gu.pci->Map || !Gu.pci->Unmap)
    return EFI_UNSUPPORTED;
  s=Gu.bs->AllocatePool(EfiLoaderData,sizeof(GU_DMA),&metadata);
  GuLog("STAGE60 AllocatePool: bytes=%zu status=%#llx ptr=%#llx\n",
    (size_t)sizeof(GU_DMA),(unsigned long long)s,(unsigned long long)(UINTN)metadata);
  if(EFI_ERROR(s)) {
    if(metadata)*Uncertain=TRUE;
    return s;
  }
  if(!metadata){*Uncertain=TRUE;return EFI_COMPROMISED_DATA;}
  s=Gu.bs->FreePool(metadata);
  GuLog("STAGE60 FreePool: status=%#llx\n",(unsigned long long)s);
  if(EFI_ERROR(s)){*Uncertain=TRUE;return s;}
  s=Gu.pci->AllocateBuffer(Gu.pci,AllocateAnyPages,EfiBootServicesData,pages,&cpu,0);
  GuLog("STAGE60 AllocateBuffer: pages=%zu bytes=%zu status=%#llx host=%#llx\n",
    (size_t)pages,(size_t)Bytes,(unsigned long long)s,(unsigned long long)(UINTN)cpu);
  if(EFI_ERROR(s)) {
    if(cpu)*Uncertain=TRUE;
    return s;
  }
  if(!cpu){*Uncertain=TRUE;return EFI_COMPROMISED_DATA;}
  s=Gu.pci->Map(Gu.pci,EfiPciIoOperationBusMasterCommonBuffer,cpu,&mapped,&bus,&mapping);
  GuLog("STAGE60 Map: status=%#llx requested=%zu mapped=%zu dma_bus=%#llx token=%#llx\n",
    (unsigned long long)s,(size_t)requested,(size_t)mapped,
    (unsigned long long)bus,(unsigned long long)(UINTN)mapping);
  raw=s;
  if(mapping) {
    clean=Gu.pci->Unmap(Gu.pci,mapping);
    GuLog("STAGE60 Unmap: status=%#llx\n",(unsigned long long)clean);
    if(EFI_ERROR(clean)){*Uncertain=TRUE;return clean;}
  } else if(!EFI_ERROR(s) || bus || mapped!=requested) {
    /* Firmware returned an ambiguous mapping without an unmap token. */
    GuLog("STAGE60 missing Map token or partial result: refusing cleanup/handoff\n");
    *Uncertain=TRUE;return EFI_COMPROMISED_DATA;
  }
  /* Map must return a full-size, 32-bit-aligned coherent bus address for this
   * particular 32-bit booter layout. A reversible unsupported mapping need
   * not block normal Windows, but is not an unlock success. */
  if(!EFI_ERROR(s) && (mapped!=requested || !bus || (bus&4095U) ||
      bus>0xffffffffULL || (requested-1U)>0xffffffffULL-bus))raw=EFI_UNSUPPORTED;
  clean=Gu.pci->FreeBuffer(Gu.pci,pages,cpu);
  GuLog("STAGE60 FreeBuffer: status=%#llx\n",(unsigned long long)clean);
  if(EFI_ERROR(clean)){*Uncertain=TRUE;return clean;}
  return raw;
}
#endif
