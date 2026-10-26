/* SPDX-License-Identifier: GPL-2.0-only */
/* Stage35: diagnostic classification only. No return value authorizes a GPU write.
 * Historical captures are NOT a qualification or an unlock compatibility list. */
#pragma once
#include <stdint.h>

/* From NVIDIA Open RM: GPU_READ_PRI_ERROR_MASK/CODE.
 * 0xBADxxxxx identifies the broader PRI read-error response family.
 * This is a *possible* bad register read, not a diagnosis of the cause.
 * Fail closed whenever it appears on a transaction-critical MMIO read. */
static inline int Gu35PriReadError(uint32_t v) {
  return (v & UINT32_C(0xFFF00000)) == UINT32_C(0xBAD00000);
}
static inline int Gu35SuspiciousMmio(uint32_t v) {
  return Gu35PriReadError(v) || v == UINT32_C(0xFFFFFFFF);
}
/* Exact selector pairs seen in CAP0003/Stage31 and observed subsequently.
 * Stage54: add ONLY 0x21605026/4, observed in the 2026-10-26
 * physical Stage53 read-only cold-profile STOP log (mutated=0).
 * This recognition is observational, NOT proof that SEC2, FWSEC,
 * GOP driver release, FLR or GPU recovery are safe on this card.
 * Do not expand to a Cartesian product or wildcard SS0 high byte. */
static inline int Gu35HistoricallyObservedSelectors(uint32_t ss0, uint32_t ss1) {
  return (ss0 == UINT32_C(0x25605026) && (ss1 == 4 || ss1 == 0)) ||
         (ss0 == UINT32_C(0x21605026) && (ss1 == 0 || ss1 == 4));
}
/* Can these reads serve as normal SEC2 control readbacks? This does NOT mean
 * SEC2 is initialized, powered, quiescent, or ready for firmware operations. */
static inline int Gu35Sec2ReadsNonPoison(uint32_t cpuctl, uint32_t irqstat,
                                          uint32_t dmactl, uint32_t reset_plm) {
  return !Gu35SuspiciousMmio(cpuctl) && !Gu35SuspiciousMmio(irqstat) &&
         !Gu35SuspiciousMmio(dmactl) && !Gu35SuspiciousMmio(reset_plm);
}
