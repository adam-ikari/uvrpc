# UVRPC DSL Code Generator
# PyInstaller build configuration with glibc bundling
# Usage: pyinstaller --onefile --clean rpc_dsl_generator_bundle.spec

import os
import sys

block_cipher = None

# Get the directory containing this spec file
spec_root = os.path.dirname(os.path.abspath(SPEC))

# Find glibc version
import subprocess
try:
    glibc_version = subprocess.check_output(['ldd', '--version'], stderr=subprocess.STDOUT).decode()
    glibc_version = glibc_version.split('\n')[0].split()[-1]
except:
    glibc_version = "unknown"

print(f"Detected glibc version: {glibc_version}")

a = Analysis(
    ['rpc_dsl_generator.py'],
    pathex=[spec_root],
    binaries=[
        # Bundle glibc libraries for maximum portability
        # Note: This requires glibc to be statically linked or using a tool like musl
        # For production use, consider using a Docker container with specific glibc version
    ],
    datas=[
        # Include templates directory
        ('templates', 'templates'),
    ],
    hiddenimports=[
        'jinja2',
        'jinja2.filters',
        'jinja2.ext',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='uvrpc-gen',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='uvrpc-gen',
)