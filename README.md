# GPU Unlock Ada on Windows — Experimental AD103 / Stage 62

**Research snapshot • 26 October 2026 • Single tested system • No stable release**

Experimental UEFI / Windows x64 research on a **Gigabyte GeForce RTX 4070 Ti SUPER Windforce OC 16 GB (NVIDIA AD103, PCI ID `10DE:2705`)**. This repository contains the **Stage 62 C source**, offline build/verification scripts, test documentation and third-party license notices.

> [!CAUTION]
> This is **active, hardware-mutating UEFI research code**, not a safe universal GPU unlocker. A built EFI program may leave the system without video output or an operable boot path. The active Stage 62 configuration has **no validated rollback or AMD fallback**. Do not execute a generated EFI payload on the basis of these results.

## Snapshot at a glance

| Topic | Details |
|---|---|
| GPU | Gigabyte RTX 4070 Ti SUPER Windforce OC 16 GB, AD103 |
| Hardware profile | PCI `10DE:2705`; subsystem `1458:4136` |
| VBIOS used | Cross-flashed AORUS firmware (**not** stock Windforce) |
| Motherboard | MSI PRO B650S WIFI |
| Software observed | Windows 11 x64; NVIDIA driver 621.01; CUDA Toolkit 13.4 |
| Experimental milestone | Stage 62 — DMA identity mapping / null mapping token handling |
| Observed outcome | Local log reported `Transaction result=0` / `TRANSACTION OK`; Windows subsequently started |
| Validation scope | One computer and one experimental firmware profile; no cross-device qualification |

Successful status messages describe **one observation**, not proof of a generally reliable unlock or quantified performance gain.

## Synthetic CUDA MMA results

| Operation | Reported peak |
|---|---:|
| FP16 → FP16 | 190.6 TFLOPS |
| FP16 → FP32 | 190.5 TFLOPS |
| BF16 → FP32 | 189.9 TFLOPS |
| INT8 → INT32 | 379.9 TOPS |
| FP32 FFMA | 45.6 TFLOPS |

These are **synthetic instruction-level peaks**, not measured gaming FPS, DLSS or real application results. The FP32 FFMA figure is present in an original local PowerShell MMA transcript; Tensor-mode peaks are taken from the contemporaneous test summary. There is **no controlled, same-card pre-unlock baseline** demonstrating a specific speedup on this machine.

Details: [results and limitations](docs/RESULTS.md) · [machine-readable summary](data/benchmark-summary.csv)

## Ollama BF16 LLM follow-up (27 October 2026)

On `qwen3.5:4b-bf16` (Ollama 0.40.11, reported 100% GPU), the operator reported **7,901.9 prefill tokens/s OFF** versus **10,576.2** and **10,081.7 tokens/s ON** in two selected three-run series (**+33.8%** and **+27.6%**). Decode throughput did **not** improve. See [test details](docs/RESULTS.md#4-ollama-llm-application-benchmark--operator-reported-comparison-2026-10-27) and [CSV](data/ollama-benchmark-summary.csv).

## Repository guide

| Resource | Purpose |
|---|---|
| [`src/`](src/) | Stage 62 UEFI research code, adapter, host-test fixture and vendored headers |
| [`tools/`](tools/) | Offline builder and verifier (builder outputs an **active** EFI program) |
| [Build / offline verification](docs/BUILD.md) | Prerequisites, source checks and explicit build risk |
| [Architecture](docs/ARCHITECTURE.md) | Layout, guarded firmware flow and Stage 62 change |
| [Safety & scope](docs/SAFETY.md) | Hardware risks, evidence boundaries and redistribution |
| [Experimental results](docs/RESULTS.md) | Synthetic measurements, methodology and caveats |
| [Changelog](CHANGELOG.md) | Research milestone notes |
| [Polska wersja](README_PL.md) | Dokumentacja po polsku |

A GitHub Actions source-only workflow performs a syntax check and checks for disallowed binary/private paths. **It does not build, flash or execute firmware and is not a hardware safety test.**

## Reproducibility and excluded assets

The source and test scripts are available, but building the active branch requires a *separately reviewed, private* machine-specific `src/local/boot_profile.h`. The repository deliberately excludes firmware blobs, VBIOS/PROM dumps, compiled EFI artifacts, private partition identifiers, GPU UUIDs and raw system logs. See [BUILD.md](docs/BUILD.md) for offline-only instructions.

Do not treat a passing host test, profile pin or signed-off commit as a safety or compatibility certification. No universal installation or recovery instructions are offered.

## Upstream and license notices

The work derives from [gpu-unlock-ada](https://github.com/JustAResearcher/gpu-unlock-ada) and [Windows-GPU-UNLOCK](https://github.com/impacifist/Windows-GPU-UNLOCK), with additional earlier work acknowledged in [third-party notices](THIRD_PARTY_NOTICES.md) and [upstream Windows notices](UPSTREAM-WINDOWS-NOTICES.md). Core research sources retain GPL-2.0-only terms, and EDK II / other materials retain their respective license notices in [LICENSES/](LICENSES/).

**Not affiliated with or endorsed by NVIDIA.**
