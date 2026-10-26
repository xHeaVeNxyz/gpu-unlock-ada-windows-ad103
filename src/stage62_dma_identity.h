/* Stage62: a narrow rule for MSI firmware returning successful PCI Map with
 * NULL Mapping handle. This does not verify hardware DMA or FWSEC. */
#ifndef GU_STAGE62_DMA_IDENTITY_H
#define GU_STAGE62_DMA_IDENTITY_H
static BOOLEAN Gu62IdentityMappingOk(VOID *Cpu, UINTN Bytes, UINTN Mapped, UINT64 Bus) {
  UINT64 Host=(UINT64)(UINTN)Cpu;
  return Cpu && Bytes && Bytes<=0xffffffffULL && Mapped==Bytes &&
    Bus==Host && !(Bus&4095ULL) && Bus<=0xffffffffULL &&
    Bytes-1ULL<=0xffffffffULL-Bus;
}
#endif
