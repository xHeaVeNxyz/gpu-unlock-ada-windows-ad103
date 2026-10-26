# Build and local verification (development only)

> **Caution:** Stage 62 is an **active, hardware-mutating UEFI research build**. A successful compilation or unit test is not evidence that it can be safely booted. This document covers offline build and inspection only. It does not describe installing, booting, flashing, or applying the payload to hardware.

## Repository status

- `src/`: experimental Windows x64 UEFI source, including Stage 62 DMA-identity mapping changes.
- `tools/build_stage62.py`: compiles and links an EFI application. The build script explicitly enables `GU_AD104_TRANSACTION`, `GU_ENABLE_RISKY_ACTIVE`, `GU_EXPERIMENT_HEADLESS_NVIDIA_ONLY`, and `GU_STAGE62_ACTIVE`.
- `tools/verify_stage62.py`: static checks, a GCC host test of `Gu62IdentityMappingOk` integration, and PE-file validation of the previously built artifact.
- `src/boot_profile.h`: includes a **local, untracked** `src/local/boot_profile.h` for a machine-specific partition identifier. Its host-test GUID is a fixture, not a valid deployment configuration.
- Firmware assets and ROM/PROM captures are **not** distributed here.

## Prerequisites

For offline compilation on Windows:
- Python 3.
- LLVM `clang` and `lld-link` on the `PATH`; the build script uses the `x86_64-pc-windows-msvc` target.
- A reviewed, non-public `src/local/boot_profile.h` containing the required `GU_WINDOWS_PARTITION_INITIALIZER`. The sample `src/boot_profile.example.h` deliberately contains an all-zero unusable value; do not substitute an arbitrary identifier.
- For `tools/verify_stage62.py`, a compatible GCC (e.g., MinGW-w64) is additionally required to compile and run the host test.

The repository intentionally does **not** ship a working machine-specific boot configuration. Simply cloning it may fail to compile, by design.

## Offline commands

From the repository root:

```powershell
python -m py_compile tools/build_stage62.py tools/verify_stage62.py src/tools/pe_format.py
```

This only checks Python syntax and does not compile or run EFI code.

**For experienced maintainers working in an isolated development environment only**, the existing scripts are:

```powershell
python tools/build_stage62.py
python tools/verify_stage62.py
```

The first command produces `src/build/BOOTX64.EFI` and uses the risky ACTIVE defines listed above. **Do not run that EFI file** as part of this verification. The second command requires the compiled artifact and runs a host-side test plus PE format validation; it does not validate real firmware/device safety.

No builds or scripts are claimed to have been independently executed as part of this documentation update.

## Security and reproducibility

Do not commit `src/local/`, personal partition identifiers, firmware images, VBIOS/ROM dumps, GPU UUIDs, full unredacted capture logs, or generated `BOOTX64.EFI`. The `.gitignore` excludes common forms; it does not replace reviewing the staged files.

Before considering any release, review [Safety & scope](SAFETY.md), [Experimental results](RESULTS.md), [Architecture](ARCHITECTURE.md), and all applicable [third-party license notices](../THIRD_PARTY_NOTICES.md).
