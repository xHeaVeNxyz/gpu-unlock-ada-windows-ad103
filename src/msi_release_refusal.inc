/* SPDX-License-Identifier: GPL-2.0-only */
/* Stage29: This is the production definition of GuMsiRelease(), included by
 * msi_release.c AND compiled directly by the host regression harness. It is
 * deliberately NOT an EFI unlock or a disconnected-owner implementation.
 */
EFI_STATUS GuMsiRelease(VOID) {
  /* STAGE29_NO_PHYSICAL_RELEASE: owners are not qualified for detachment. */
  if (GuBootLogOpen()) GuLog("Stage29: physical disconnect explicitly disabled for AORUS AD103\n");
  return EFI_SECURITY_VIOLATION;
}
