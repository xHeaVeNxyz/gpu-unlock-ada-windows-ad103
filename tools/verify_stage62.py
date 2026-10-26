from pathlib import Path
import subprocess,shutil,tempfile,hashlib,sys,json,os
r=Path(__file__).resolve().parents[1];src=r/'src'
p=(src/'port.c').read_text();header=(src/'stage62_dma_identity.h').read_text();app=(src/'app.c').read_text();b=(r/'tools/build_stage62.py').read_text();boot=(src/'boot_io.c').read_text()
checks={
 'production_helper_integrated':'Gu62IdentityMappingOk(Entry->cpu,Entry->bytes,Mapped,Entry->dma)' in p,
 'specific_null_identity_only':'if(!EFI_ERROR(Status) && !Entry->mapping)' in p and 'if(!Gu62IdentityMappingOk' in p,
 'failed_null_map_kept_fail_closed':'if(EFI_ERROR(Status) && !Entry->mapping' in p,
 'mapped_bit_after_success':'if(!EFI_ERROR(Status) || Entry->mapping)Entry->mapped=TRUE;' in p,
 'unmap_always_called_on_free':'Status=Gu.pci->Unmap(Gu.pci,Entry->mapping);' in p,
 'failed_unmap_retained':'if(EFI_ERROR(Status)) {Gu.retained=TRUE;return Status;}' in p,
 'active_build_is_explicit':'-DGU_STAGE62_ACTIVE' in b and '-DGU_EXPERIMENT_HEADLESS_NVIDIA_ONLY' in b and '-DGU_STAGE55_READONLY' not in b,
 'proper_menu':'AD103 Stage62' in boot,
 'proper_banner':'AD103 STAGE62 NVIDIA-ONLY ACTIVE' in app,
 'other_guards_retained':'Gu.retained=TRUE;Gu.fault=TRUE' in p,
}
for k,v in checks.items():print(('PASS' if v else 'FAIL')+' '+k)
if not all(checks.values()):sys.exit(1)
cc=os.environ.get('STAGE62_GCC') or (str(Path('C:/msys64/ucrt64/bin/gcc.exe')) if os.name=='nt' and Path('C:/msys64/ucrt64/bin/gcc.exe').exists() else shutil.which('gcc'))
if not cc:sys.exit('gcc missing')
with tempfile.TemporaryDirectory(prefix='stage62-') as td:
 exe=Path(td)/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-std=c11','-O0','-fshort-wchar','-ffunction-sections','-fdata-sections','-D__KERNEL__','-DGU_UEFI_PORT','-DGU_AD104_HOST_TEST',
 '-I'+str(src),'-I'+str(src/'compat'),'-I'+str(src/'vendor/edk2/Include'),'-I'+str(src/'vendor/edk2/Include/X64'),
 str(src/'port.c'),str(src/'tests/test_stage62_dma_identity.c'),'-Wl,--no-gc-sections',
 ('-Wl,--image-base,0x10000000' if os.name=='nt' else '-no-pie'),'-o',str(exe)]
 cp=subprocess.run(cmd,capture_output=True,text=True)
 if cp.returncode:sys.exit('compile failed:\n'+cp.stderr)
 cp=subprocess.run([str(exe)],capture_output=True,text=True)
 print(cp.stdout,end='')
 if cp.returncode:sys.exit('test failed:\n'+cp.stderr)
# Verify active package contents, not a diagnostic EFI.
from importlib import import_module
sys.path.insert(0,str(src/'tools'))
from pe_format import validate_pe
f=src/'build/BOOTX64.EFI';data=f.read_bytes();validate_pe(data)
print('STAGE62 PASS OFFLINE, ACTIVE HARDWARE UNTESTED; EFI_SHA256 '+hashlib.sha256(data).hexdigest())
