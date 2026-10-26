/* SPDX-License-Identifier: GPL-2.0-only */
/* EXPERIMENTAL physical UEFI detach, specifically two observed owners:
 * GraphicsConsole on NVIDIA child, nvgop-ad1xx on NVIDIA PCI root.
 * This is a high-risk one-way transaction.  NO RECONNECT / NO ROLLBACK.
 * Image identity is metadata ONLY; there are no reviewed image hashes.
 * Never invoke Stop() directly; UEFI DisconnectController owns the graph.
 */
EFI_STATUS GuMsiRelease(VOID) {
  GU_DETACH_STEP steps[2];
  EFI_STATUS s;
  if(!Plan.ready||Plan.mutated||Plan.stage||Plan.claimed||
     Gu.started||Gu.owned||Gu.retained||Gu.fault||!Gu.profile_verified||
     !Gu.prom_verified||!GuBootLogOpen()||!GuAd104ColdEntry())
    return EFI_NOT_READY;
  s=TwoOwnerPlan(steps);
  if(s!=EFI_SUCCESS)return s;
  if(!Plan.amd_pci||Plan.foreign_count==0)return EFI_SECURITY_VIOLATION;
  for(UINTN i=0;i<2;i++) {
    const UINTN pre=i?1u:0u;
    const UINTN post=i?3u:1u;
    UINT32 c[64];
    if((s=ReadConfig(c))!=EFI_SUCCESS||!Equal(c,Plan.config,sizeof(c))||
       !GuAd104ColdEntry()||Alive()!=EFI_SUCCESS||Gops(FALSE)!=EFI_SUCCESS||
       (s=Snapshot(&Current))!=EFI_SUCCESS||!Expected(pre,FALSE)){
      NOTE("two-owner PRE-check failed step=%u: release not attempted for this step\n",(unsigned)i);
      return s==EFI_SUCCESS?EFI_NOT_READY:s;
    }
    /* Qualify driver and callback identity before touching it. */
    s=Driver(i+1);
    if(s!=EFI_SUCCESS)return s;
    /* The return status is NOT proof that a driver stopped. Record the
     * attempt BEFORE invoking UEFI; a failure may have changed the graph.
     * Once issued, any failure permanently prohibits Windows handoff.
     */
    Plan.mutated=TRUE;
    GuLog("EXPERIMENTAL GOP detach: step=%u controller=%#llx driver=%#llx\n",
      (unsigned)i,(unsigned long long)(UINTN)steps[i].controller,
      (unsigned long long)(UINTN)steps[i].driver);
    s=Plan.table->BootServices->DisconnectController(steps[i].controller,
      steps[i].driver,steps[i].child);
    NOTE("GOP detach returned step=%u status=%#llx\n",(unsigned)i,(unsigned long long)s);
    if(s!=EFI_SUCCESS||Gu.fault)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    Plan.stage=post;
    s=Snapshot(&Current);
    if(s==EFI_SUCCESS&&!Expected(post,FALSE))s=EFI_NOT_READY;
    if(s==EFI_SUCCESS)s=Alive();
    if(s==EFI_SUCCESS)s=Gops(FALSE);
    if(s!=EFI_SUCCESS||Gu.fault) {
      NOTE("GOP detach postcondition failed step=%u\n",(unsigned)i);
      return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    }
  }
  /* The UEFI Stop() on the PCI root can turn off IO/MEM decoding.
   * This function permits only restoration of ORIGINAL decode bits and
   * checks full config+owner graph before enabling GPU transaction. */
  s=ClaimRestoreTwoOwner();
  if(s==EFI_SUCCESS&&!GuMsiReleased())s=EFI_NOT_READY;
  return s;
}
