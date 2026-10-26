# Experimental GPU Unlock Ada — Windows UEFI AD103 / RTX 4070 Ti SUPER

**Status:** Stage 62 research snapshot, single-machine experiment (2026-10-26). Not a general-purpose unlocker or a ready-to-run release.

This repository documents experiments with an Ada-generation NVIDIA GeForce RTX 4070 Ti SUPER (AD103, PCI ID `10DE:2705`) and a Windows x64 UEFI port derived from [GPU Unlock Ada](https://github.com/JustAResearcher/gpu-unlock-ada) and [Windows-GPU-UNLOCK](https://github.com/impacifist/Windows-GPU-UNLOCK).

> **Hardware risk:** This experimental UEFI path can affect firmware execution, GPU/DMA state, PCIe function reset and the Windows graphics handoff. It can result in no video output or an unbootable system. There is no validated universal rollback procedure. Cloning or reading the repository does not execute the firmware.

## Tested configuration

| Component | Test system |
|---|---|
| Graphics card | Gigabyte GeForce RTX 4070 Ti SUPER Windforce OC 16 GB (AD103) |
| PCI device | `10DE:2705` |
| Motherboard | MSI PRO B650S WIFI |
| Operating system | Windows 11 x64 |
| NVIDIA driver during benchmark | 621.01 |
| CUDA | CUDA Toolkit 13.4 |
| Firmware context | Cross-flashed AORUS VBIOS; **do not assume stock-card compatibility** |
| Experiment date | 26 October 2026 |

The Stage 62 experimental log reported `Transaction result=0` and `TRANSACTION OK`. Windows subsequently initialized and detected the NVIDIA GPU. Those are **observations from one system**, not independent proof of general compatibility or long-term stability.

## CUDA MMA benchmark

Selected peaks observed after the experiment:

| CUDA MMA mode | Peak | Unit |
|---|---:|---|
| FP16 → FP16 | 190.6 | TFLOPS |
| FP16 → FP32 | 190.5 | TFLOPS |
| BF16 → FP32 | 189.9 | TFLOPS |
| INT8 → INT32 | 379.9 | TOPS |
| FP32 FFMA | 45.6 | TFLOPS |

**Interpretation:** These are instruction-level measurements, **not** real-world FPS, gaming, DLSS, LLM or GEMM throughput guarantees. No same-card, same-conditions pre-unlock baseline was preserved. Therefore a measured 2× speedup **on this card** is **not established**. See [the benchmark methodology and limitations](docs/RESULTS.md).

## Source code and provenance

The Stage 62 source package was prepared separately with its `src/`, `tools/`, license notices and safety checks. The complete code will be published in this repository only after sanitized-source transfer; do not treat a documentation-only checkout as a buildable release. This repository will not include GPU ROM dumps, NVIDIA firmware images, private boot partition GUIDs, machine-unique IDs or compiled EFI payloads.

The underlying works retain their own licenses. Original GPU Unlock Ada and UEFI port: GPL-2.0-only; separately licensed vendor and EDK II components must retain their notices. No NVIDIA endorsement is implied.

## Reproducibility and limitations

- Observed with one specific AD103 board and cross-flashed firmware profile.
- No cross-machine replication or controlled same-device before/after run.
- Firmware execution, DMA, PCIe FLR and graphics handoff are hazardous and require independent hardware/firmware safety review.
- Do not install or boot experimental EFI files solely on the basis of the benchmark table.
- Raw Windows PowerShell transcripts and GPU dumps are intentionally not public because they can contain device identifiers and user paths.

[Dokumentacja po polsku](README_PL.md)
