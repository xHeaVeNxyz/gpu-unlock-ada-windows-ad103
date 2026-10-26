# Safety, security and publication scope

**Danger: experimental low-level UEFI code. Not recommended for general use.**

This project includes an ACTIVE path that can change GPU and platform state before Windows starts. It may:
- lose graphics output or block Windows from completing startup;
- alter firmware-processor, PCIe and DMA state;
- leave the device in a state that ordinary OS recovery cannot restore;
- behave differently after changing VBIOS, motherboard UEFI, peripherals or drivers.

The experimental Stage 62 boot banner specifically states **no AMD fallback and no rollback**. No broadly validated recovery process is included.

## What has been observed

A single machine reported a successful Stage 62 transaction status and reached Windows afterwards. Reported CUDA instruction-level performance figures are in [RESULTS.md](RESULTS.md). This is **not** proof of portability, security, or long-term stability.

## What has *not* been established

- Successful, repeatable operation on stock Windforce firmware or other RTX 4070 Ti SUPER boards.
- A validated recovery plan or compatibility matrix.
- A controlled same-GPU before/after benchmark showing a quantified speedup.
- Safety of running the generated `BOOTX64.EFI` on other systems.
- Improved game FPS, DLSS, frame generation, or application-level throughput.

## Private data and redistribution

Keep out of the repository:
- machine-specific `src/local/boot_profile.h` and real EFI partition identifiers;
- GPU firmware and VBIOS/PROM binaries;
- unsigned/compiled EFI payloads, drivers, or copied commercial firmware images;
- raw Windows transcripts with user directories, GPU UUIDs, partition information or unrelated system identifiers;
- credentials, tokens and other secrets.

A `.gitignore` entry does **not** protect an already committed file. Review the git history and staged content before making a repository public. Licensing and redistribution rights for third-party assets must be reviewed separately from source-code licensing.

## No endorsement

This is community research and is not an NVIDIA driver, commercial product, validated hardware modification or manufacturer-endorsed instruction set.
