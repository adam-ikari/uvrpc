# UVRPC DSL Code Generator
# PyInstaller build configuration - Single file optimized
# Usage: pyinstaller --clean rpc_dsl_generator_standalone.spec

import os
import sys

block_cipher = None

# Get the directory containing this spec file
spec_root = os.path.dirname(os.path.abspath(SPEC))

a = Analysis(
    ['rpc_dsl_generator_with_flatcc.py'],
    pathex=[spec_root],
    binaries=[],
    datas=[
        # Include templates directory
        ('templates', 'templates'),
    ],
    hiddenimports=[
        'jinja2',
        'jinja2.filters',
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
    strip=True,
    upx=True,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)