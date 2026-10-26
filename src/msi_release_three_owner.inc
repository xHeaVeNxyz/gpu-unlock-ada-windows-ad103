/* SPDX-License-Identifier: GPL-2.0-only */
/* Stage56 - EXPERIMENTAL physical UEFI THREE-owner detach.
 * 1 ConSplitter on NVIDIA child TextOut (never global image),
 * 2 GraphicsConsole on NVIDIA child GOP, 3 NVIDIA driver on PCI root.
 * NOTE: NO AUTOMATIC ROLLBACK, NO VERIFIED HARDWARE TRANSACTION.
 */
EFI_STATUS GuMsiRelease(VOID){
  GU_DETACH_STEP steps[3];
  EFI_STATUS s;
  if(!Plan.ready||Plan.mutated||Plan.stage||Plan.claimed||
     Gu.started||Gu.owned||Gu.retained||Gu.fault||!Gu.profile_verified||
     !Gu.prom_verified||!GuBootLogOpen()||!GuAd104ColdEntry())
    return EFI_NOT_READY;
  s=ThreeOwnerPlan(steps);
  if(s!=EFI_SUCCESS)return s;
  for(UINTN i=0;i<3;i++){
    UINT32 c[64];
    if((s=ReadConfig(c))!=EFI_SUCCESS||!Equal(c,Plan.config,sizeof(c))||
       !GuAd104ColdEntry()||Alive()!=EFI_SUCCESS||Gops(FALSE)!=EFI_SUCCESS||
       (s=Snapshot(&Current))!=EFI_SUCCESS||!Expected(i,FALSE)){
      NOTE("Stage56 BEFORE detach: failed step=%u; no attempt for current step\n",(unsigned)i);
      return s==EFI_SUCCESS?EFI_NOT_READY:s;
    }
    s=Driver(i);
    if(s!=EFI_SUCCESS)return s;
    /* Callback could mutate device and return an error: record BEFORE call. */
    Plan.mutated=TRUE;
    GuLog("Stage58 HEADLESS EXPERIMENTAL detach: step=%u controller=%#llx owner=%#llx NO ROLLBACK\n",
      (unsigned)i,(unsigned long long)(UINTN)steps[i].controller,
      (unsigned long long)(UINTN)steps[i].driver);
    s=Plan.table->BootServices->DisconnectController(
      steps[i].controller,steps[i].driver,steps[i].child);
    NOTE("Stage56 detach returned step=%u status=%#llx\n",(unsigned)i,(unsigned long long)s);
    if(s!=EFI_SUCCESS||Gu.fault)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    Plan.stage=i+1;
    s=Snapshot(&Current);
    if(s==EFI_SUCCESS&&!Expected(i+1,FALSE))s=EFI_NOT_READY;
    if(s==EFI_SUCCESS)s=Alive();
    if(s==EFI_SUCCESS)s=Gops(FALSE);
    if(s!=EFI_SUCCESS||Gu.fault){
      NOTE("Stage56 AFTER detach: postcondition failed step=%u\n",(unsigned)i);
      return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    }
  }
  s=ClaimRestoreTwoOwner(); /* name legacy; reads Expected(3) with updated graph */
  if(s==EFI_SUCCESS&&!GuMsiReleased())s=EFI_NOT_READY;
  return s;
}
