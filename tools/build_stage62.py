#!/usr/bin/env python3
"""Rebuilds Stage62 ACTIVE NVIDIA-only (no iGPU) experimental x64 EFI. Requires clang + lld-link; NO hardware calls."""
import os, shutil, subprocess
from pathlib import Path
src=Path(__file__).resolve().parents[1]/'src'
out=src/'build';out.mkdir(parents=True,exist_ok=True)
clang=os.environ.get('CLANG') or shutil.which('clang')
linker=os.environ.get('LLD_LINK') or shutil.which('lld-link')
if not clang or not linker:raise SystemExit('clang and lld-link required')
sources=['app','boot_io','msi_release','port','compat','checks','profile','sha256','runtime','format','vendor/unlock_610','vendor/prep_610','vendor/layout_610','vendor/vbios_610','vendor/gfw_eval']
flags=['--target=x86_64-pc-windows-msvc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-D__KERNEL__','-DGU_UEFI_PORT','-DGU_AD104_TRANSACTION','-DGU_ENTRY_LATCHED_SS1_FULL','-DGU_ENABLE_RISKY_ACTIVE','-DGU_EXPERIMENT_HEADLESS_NVIDIA_ONLY','-DGU_STAGE62_ACTIVE','-Icompat','-Ivendor/edk2/Include','-Ivendor/edk2/Include/X64','-ffreestanding','-fno-builtin','-fno-stack-protector','-mno-stack-arg-probe','-mno-red-zone','-fshort-wchar']
obj=[]
for name in sources:
  file=out/(name.replace('/','_')+'.obj')
  subprocess.run([clang,*flags,'-c',name+'.c','-o',str(file)],cwd=src,check=True)
  obj.append(str(file))
exe=out/'BOOTX64.EFI'
subprocess.run([linker,'/nologo','/subsystem:efi_application','/entry:UefiMain','/machine:x64','/nodefaultlib','/incremental:no','/fixed:no','/dynamicbase:no','/nxcompat:no','/brepro','/base:0x180000000','/out:'+str(exe),*obj],cwd=src,check=True)
import sys
sys.path.insert(0,str(src/'tools'))
from pe_format import validate_pe
validate_pe(exe.read_bytes())
print('Built',exe,'bytes=',exe.stat().st_size)
