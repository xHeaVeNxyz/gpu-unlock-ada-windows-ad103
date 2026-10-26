# Experimental AD103 / Stage 62 observations

**Date:** 2026-10-26  
**GPU:** Gigabyte RTX 4070 Ti SUPER Windforce OC 16 GB, AD103 / `10DE:2705`  
**Motherboard:** MSI PRO B650S WIFI  
**Firmware:** Cross-flashed AORUS VBIOS  
**Software:** Windows 11 x64, NVIDIA driver 621.01, CUDA Toolkit 13.4.

## UEFI transaction

The recorded Stage 62 test reported `Transaction result=0`, `safe=1`, `started=1`, `quiesced=1`, `flrs=1`, `DMA_live=0`, and `TRANSACTION OK`. Windows subsequently booted and recognized the NVIDIA GPU. These are local, single-system observations, and the meaning of internal status fields relies on the experimental implementation.

## Peak instruction benchmark

| Instruction / mode | Run A best | Run B best | Best overall | Unit |
|---|---:|---:|---:|---|
| FP16 → FP16 | 190.5 | 190.6 | **190.6** | TFLOPS |
| FP16 → FP32 | 190.5 | 190.0 | **190.5** | TFLOPS |
| BF16 → FP32 | 189.8 | 189.9 | **189.9** | TFLOPS |
| INT8 → INT32 | 379.9 | 379.9 | **379.9** | TOPS |
| FP32 FFMA | 45.6 | 45.5 | **45.6** | TFLOPS |

Reported benchmark setup: upstream-style `mma_bench.cu`, Ada SM89, CUDA 13.4, MSVC Build Tools 2022, 20,000 iterations and 3 repetitions per invocation.

### Caveats

- This is an instruction-oriented synthetic benchmark, not an application benchmark.
- No measured pre-unlock baseline for the *same GPU under controlled conditions* was preserved; therefore a direct 2× speedup on this machine cannot be claimed.
- Long-term stability and repeatability on other cards/motherboards/VBIOS revisions have not been established.
- The raw transcripts contain local user paths and device UUIDs and are intentionally omitted.
- Do not infer that a documented benchmark makes the experimental UEFI binary safe to execute.

## Source and credits

Derived experimental work; upstream credits: [JustAResearcher/gpu-unlock-ada](https://github.com/JustAResearcher/gpu-unlock-ada) and [impacifist/Windows-GPU-UNLOCK](https://github.com/impacifist/Windows-GPU-UNLOCK). Preserve original license notices with source publication.
