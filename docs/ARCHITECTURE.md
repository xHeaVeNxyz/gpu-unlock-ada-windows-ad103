# Architecture and source map

This is a **hardware-specific research branch**, not an upstream-supported driver or a universal Ada unlocker.

## Project layout

| File or directory | Responsibility |
|---|---|
| `src/app.c` | UEFI entry point, target GPU discovery and transaction orchestration |
| `src/boot_io.c`, `src/boot_io.h` | USB-backed logging, device-path checks, Windows boot-image staging/handoff |
| `src/profile.c`, `src/profile.h` | Strict PCI/MMIO and PROM/firmware profile validation for the observed device |
| `src/port.c`, `src/port.h` | PCI I/O, DMA, FLR, reset-related and transaction-adapter operations |
| `src/msi_release.c`, `src/msi_release.h` | Experimental GOP/controller ownership checks |
| `src/vendor/` | Imported or adapted upstream firmware-side code and EDK II headers; see notices |
| `src/stage59_dma_probe.h` through `src/stage62_dma_identity.h` | Research stages associated with DMA allocation, mapping and identity checks |
| `src/tests/test_stage62_dma_identity.c` | Host-only test for Stage 62 DMA identity mapping helper |
| `src/boot_profile.example.h` | Non-functional template for local boot configuration |
| `tools/build_stage62.py` | Experimental ACTIVE UEFI binary builder |
| `tools/verify_stage62.py` | Source assertions, host test and PE-file check |

## High-level experimental flow

1. UEFI entry point identifies the target PCI GPU and verifies the expected profile.
2. The Windows boot image is staged, with a manual choice to skip the GPU transaction.
3. The active branch performs ownership preflight, loads externally provided firmware assets and evaluates the cold profile.
4. The research path handles firmware operations, DMA and graphics ownership/FLR handling with fail-closed checks.
5. The experimental code attempts a Windows graphics handoff, subject to its internal status and failure conditions.

**This outline is not an execution guide**. Changing the board/VBIOS/firmware/UEFI platform invalidates the experimental assumptions.

## Stage 62 focus

Stage 62 addresses how an EFI PCI `Map` result with a null mapping token may represent an identity map. The implementation requires `Gu62IdentityMappingOk` before admitting that case and tracks failure states for cleanup. The offline test checks code paths; it does not prove safe DMA on arbitrary IOMMUs or motherboards.

## Hardware specificity

The source pins `10DE:2705` (AD103), the observed `1458:4136` subsystem and a specific BAR0/firmware configuration. The tested card used a cross-flashed AORUS VBIOS. Treat exact-match preflights as *necessary guards* only, not as proof of hardware safety.

## Ownership and licenses

This branch is derived from earlier GPL-2.0-only GPU unlock and Windows UEFI research work. Preserve [LICENSE](../LICENSE), [upstream notices](../UPSTREAM-WINDOWS-NOTICES.md), [third-party notices](../THIRD_PARTY_NOTICES.md), and accompanying `LICENSES/` files. This project is not affiliated with NVIDIA.
