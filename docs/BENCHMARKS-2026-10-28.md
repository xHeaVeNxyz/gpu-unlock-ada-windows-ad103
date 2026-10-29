# RTX 4070 Ti SUPER AD103 — paired GPU benchmark / Porównanie ON–OFF
**28 October 2026 · Windows 11 · NVIDIA driver 621.01 · CUDA MMA + Ollama**

> [!IMPORTANT]
> **Operator-labeled boot states, not independently verified unlock states.** In both local metadata files, `unlock_verified=false`. The launchers `START ON.cmd` / `START OFF.cmd` **only label the measurements**; they do not enable/disable GPU firmware. The result shows a repeatable performance difference in *two synthetic MMA modes*, **not proof that additional cores were unlocked**, nor a general application speedup.
>
> **PL:** ON/OFF to deklarowany tryb uruchomienia, a nie niezależne potwierdzenie odblokowania. Różnica w dwóch testach syntetycznych nie oznacza automatycznie przyspieszenia modeli LLM.

## Highlights / Najważniejsze liczby

| Metric | OFF | ON | ON vs OFF |
|---|---:|---:|---:|
| **FP16 inputs, FP32 accumulation** | 98.13 TFLOPS | **196.40 TFLOPS** | **+100.14% (2.00×)** |
| **BF16 inputs, FP32 accumulation** | 97.60 TFLOPS | **195.87 TFLOPS** | **+100.68% (2.01×)** |
| FP16 inputs, FP16 accumulation | 196.03 TFLOPS | 196.30 TFLOPS | +0.14% |
| GPT-OSS 20B decode, 512 tokens | 141.72 tok/s | 141.59 tok/s | −0.09% |
| Maximum sampled VRAM | 15,907 / 16,376 MiB | 15,981 / 16,376 MiB | 97.1% / 97.6% filled |

Values are **3-run arithmetic means** after **2 warm-up passes**, except sampled VRAM maxima. Displayed decimals are rounded; percentage differences use unrounded recorded means.

## 1 · CUDA MMA synthetic throughput

| Operation | OFF | ON | Δ ON vs OFF | Unit |
|---|---:|---:|---:|---|
| FP16 → FP32 | 98.13 | 196.40 | **+100.14%** | TFLOPS |
| BF16 → FP32 | 97.60 | 195.87 | **+100.68%** | TFLOPS |
| FP16 → FP16 | 196.03 | 196.30 | **+0.14%** | TFLOPS |
| INT8 → INT32 | 391.73 | 391.30 | **-0.11%** | TOPS |
| DP4A | 97.47 | 96.10 | **-1.40%** | TOPS |
| FP32 FFMA | 46.93 | 47.00 | **+0.14%** | TFLOPS |

The binary was the same in both sessions (reported SHA-256: `6e3b6b7d4ee3eb5a1679868a01f318ec8fec93884c20a1f8867876df67c6f005`); each pass used **20,000 iterations**. Each operation has **2 excluded warm-ups + 3 measured passes per boot state**. The MMA summary reported a **2,925 MHz average SM clock in both modes** during the synthetic run. These are instruction-level FLOPS/OPS estimates, not application benchmarks.

## 2 · Ollama generation (512 output tokens)

| Model | OFF tok/s | ON tok/s | Δ ON vs OFF | `ollama ps` placement |
|---|---:|---:|---:|---|
| `gpt-oss:20b` | 141.72 | 141.59 | -0.09% | 100% GPU |
| `gemma4:26b` | 104.93 | 105.60 | +0.64% | 20% CPU / 80% GPU |
| `qwen3.5:27b` | 11.38 | 11.42 | +0.31% | 23% CPU / 77% GPU |
| `gemma4:31b` | 12.08 | 12.10 | +0.18% | 100% GPU [*] |
| `qwen3:30b` | 92.00 | 92.66 | +0.72% | 25% CPU / 75% GPU |
| `qwen3.5:35b` | 86.87 | 86.78 | -0.11% | 37% CPU / 63% GPU |

### Additional decode test — 256 output tokens

| Model | OFF tok/s | ON tok/s | Δ |
|---|---:|---:|---:|
| `gpt-oss:20b` | 142.14 | 142.17 | +0.03% |
| `gemma4:26b` | 109.05 | 109.38 | +0.30% |
| `qwen3.5:27b` | 11.56 | 11.57 | +0.10% |
| `gemma4:31b` | 13.30 | 13.43 | +0.99% |
| `qwen3:30b` | 92.44 | 93.22 | +0.84% |
| `qwen3.5:35b` | 86.87 | 86.46 | -0.48% |

### Unique-prompt processing / Prefill

| Model | OFF tok/s | ON tok/s | Δ |
|---|---:|---:|---:|
| `gpt-oss:20b` | 7278.10 | 7596.89 | +4.38% |
| `gemma4:26b` | 2544.47 | 2530.97 | -0.53% |
| `qwen3.5:27b` | 1147.86 | 1149.36 | +0.13% |
| `gemma4:31b` | 681.35 | 682.93 | +0.23% |
| `qwen3:30b` | 3277.27 | 3309.49 | +0.98% |
| `qwen3.5:35b` | 2265.20 | 2277.04 | +0.52% |

**Method:** 6 models × 3 test types × 5 attempts × 2 operator-labeled modes = **180 LLM attempts**, of which **72 were warm-ups and 108 were measured**. All 108 measured attempts were reported successful. Context configured to 8,192 tokens; generation lengths 256 and 512; prefill mode used a per-attempt unique, deterministic input of approximately 2,800–2,850 prompt tokens to reduce repeated-prompt cache effects. Each mode ran its entire set sequentially: OFF first, ON afterward. The quantized model format and actual CUDA kernel precision **were not forced or profiled**.

**Placement caveat:** `ollama ps` described the Gemma 4 31B run as `100% GPU` even though its reported resident size was unexpectedly small compared with the model download. Treat that figure as **unverified**, not confirmed full GPU residency. Several other models explicitly used CPU offloading, which strongly affects decode throughput and makes this *not* a controlled FP16/BF16 application test.

## 3 · Sampled GPU / VRAM data

| GPU monitor metric | OFF | ON |
|---|---:|---:|
| Samples (`nvidia-smi`, about 1 second apart) | 932 | 924 |
| Reported total VRAM | 16,376 MiB | 16,376 MiB |
| Highest sampled VRAM use | **15,907 MiB (97.1%)** | **15,981 MiB (97.6%)** |
| Peak sampled GPU utilization | 100% | 100% |
| Highest sampled GPU temperature | 63.37 °C | 61.15 °C |
| Highest sampled GPU power draw | 264.24 W | 249.90 W |

Monitor samples cover all tests including model loading, not just one kernel. Sampling may **miss short peaks**. GPU average utilization across the entire session includes idle intervals, model loads, and transfers and is not a useful kernel-level utilization comparison.

## 4 · Environment and evidence boundaries

- GPU: Gigabyte RTX 4070 Ti SUPER Windforce OC, AD103, 16 GB; Windows 11 x64; NVIDIA driver 621.01. Other hardware and firmware details are described in the repository's primary [README](../README.md).
- Source series: `20261028-102107-OFF-a3f36d` and `20261028-111056-ON-a86baf` from a locally supplied `ad103 benchmark.zip`.
- Both series: `status=FINISHED`, `unlock_verified=false`; no independent check of altered hardware resource count was recorded.
- No repeated counterbalanced OFF → ON → OFF → ON cold-boot trial; clock/power/temperature and CPU offload were not rigorously held constant.
- Compare the two synthetic tensor tests with caution: impressive differences are **repeatable observations**, not yet a causal demonstration that GPU firmware exposed or accelerated extra Tensor Cores.
- No gaming FPS, DLSS, or general inference performance improvement is established by this data.
- **Privacy:** Only sanitized aggregated results are committed; GPU UUID, host name, full Windows paths, and raw hardware logs are intentionally excluded.

## 5 · Machine-readable aggregated data

- [CUDA MMA results](../data/benchmark-2026-10-28-mma.csv) — exact recorded means and standard deviations.
- [Ollama results](../data/benchmark-2026-10-28-ollama.csv) — all 6 models and all 3 LLM test types, including offload labels.
- [GPU monitor summary](../data/benchmark-2026-10-28-gpu.csv) — session extrema and sample counts.
- [Previous October 26–27 observations](RESULTS.md) — older experiment, not overwritten.

**Research-only observation; not a guarantee of UEFI safety, successful unlocking, or improved production workload performance.**
