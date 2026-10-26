# Experimental AD103 / Stage 62 observations

**Test date:** 2026-10-26  
**GPU:** Gigabyte RTX 4070 Ti SUPER Windforce OC 16 GB (AD103, PCI `10DE:2705`, subsystem `1458:4136`)  
**Motherboard:** MSI PRO B650S WIFI  
**VBIOS:** Cross-flashed AORUS profile  
**Environment:** Windows 11 x64, NVIDIA driver 621.01; reported CUDA Toolkit 13.4.

## 1. UEFI transaction observation

The local Stage 62 run log reported:
- `Transaction result=0`
- `safe=1`, `started=1`, `quiesced=1`, `flrs=1`, `DMA_live=0`
- `TRANSACTION OK`

Windows subsequently started and detected the card. Internal status fields are interpreted by the experimental firmware code; they do not amount to independent validation of FWSEC, DMA, FLR, GPU ownership transitions, or long-term stability.

## 2. CUDA synthetic benchmark — reported peak values

| Operation | Reported run A peak | Reported run B peak | Highest reported | Unit |
|---|---:|---:|---:|---|
| FP16 → FP16 | 190.5 | 190.6 | **190.6** | TFLOPS |
| FP16 → FP32 | 190.5 | 190.0 | **190.5** | TFLOPS |
| BF16 → FP32 | 189.8 | 189.9 | **189.9** | TFLOPS |
| INT8 → INT32 | 379.9 | 379.9 | **379.9** | TOPS |
| FP32 FFMA | 45.6 | 45.5 | **45.6** | TFLOPS |

**Evidence granularity:** Tensor-mode values come from the contemporaneous project test summary; the raw per-run Tensor transcripts are not contained in this repository. The original locally retained PowerShell `original-mma-20261026-190621.txt` includes `ffma 45.6 45.6/42.7/45.6`, with the benchmark returning exit code 0. That original unredacted file is intentionally not committed because it exposes user paths and a GPU UUID.

Reported original MMA configuration: `mma_bench.cu`, CUDA 13.4, Ada SM89, 20,000 iterations, three repetitions; Windows/MSVC toolchain. The FPS/application impact was not measured here.

## 3. Interpretation limits

- Instruction-level synthetic peak throughput is not equivalent to gaming FPS, DLSS, frame-generation, GEMM, or ML-training performance.
- **No measured, controlled pre-unlock baseline on the same card** is available; therefore a 2× gain **on this system** is not established.
- The figures cannot be generalized to a stock Windforce VBIOS or another AD103 card.
- Thermal/power/clock stabilization and reproducibility across cold boots were not independently verified.
- Passing this benchmark does not prove firmware execution safety.

CSV with summarized figures: [`data/benchmark-summary.csv`](../data/benchmark-summary.csv).  
Code and offline checks: [BUILD](BUILD.md). Risks: [SAFETY](SAFETY.md).

## 4. Source attribution

See the project's [license](../LICENSE), [upstream notices](../UPSTREAM-WINDOWS-NOTICES.md), and [third-party notices](../THIRD_PARTY_NOTICES.md). NVIDIA firmware binaries are not included.
