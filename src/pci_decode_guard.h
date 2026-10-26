/* Stage53: strict PCI Command / UEFI Attributes reconciliation.
 * Some UEFI PCI bus drivers report the IO attribute as enabled while
 * Command bit 0 is clear (observed on the target AD103: attrs=0x300,
 * PCI Command=0x0002).  The *only* tolerated mismatch is this additional
 * firmware-reported IO bit. It is never permission to enable IO, disable
 * memory decoding, accept BME, or tolerate a missing reported bit.
 * All hardware Command readbacks remain mandatory after changes.
 */
#pragma once
#include <Uefi.h>
#include <Protocol/PciIo.h>
static inline BOOLEAN GuPciDecodeAttributesCompatible(UINT16 Command,UINT64 Attributes,BOOLEAN *ExtraFirmwareIo) {
  const UINT64 io=EFI_PCI_IO_ATTRIBUTE_IO;
  const UINT64 memory=EFI_PCI_IO_ATTRIBUTE_MEMORY;
  const UINT64 master=EFI_PCI_IO_ATTRIBUTE_BUS_MASTER;
  const UINT64 observed=Attributes&(io|memory|master);
  const UINT64 actual=((Command&1U)?io:0ULL)|((Command&2U)?memory:0ULL)|((Command&4U)?master:0ULL);
  if(ExtraFirmwareIo)*ExtraFirmwareIo=FALSE;
  if(!(actual&memory) || (actual&master))return FALSE;
  if(observed==actual)return TRUE;
  if(!(actual&io) && observed==(actual|io)) {
    if(ExtraFirmwareIo)*ExtraFirmwareIo=TRUE;
    return TRUE;
  }
  return FALSE;
}
