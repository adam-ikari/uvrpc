# UVRPC DSL Code Generator
# PyInstaller build configuration with bundled FlatCC
# Usage: make generator

import os
import sys
import shutil

block_cipher = None

# Get the directory containing this spec file
spec_root = os.path.dirname(os.path.abspath(SPEC))

# Find FlatCC executable
FLATCC_PATH = None
FLATCC_SEARCH_PATHS = [
    '../build/flatcc/flatcc',
    '../deps/flatcc/bin/flatcc',
    '/usr/local/bin/flatcc',
    '/usr/bin/flatcc',
]

for path in FLATCC_SEARCH_PATHS:
    abs_path = os.path.join(spec_root, path)
    if os.path.exists(abs_path):
        FLATCC_PATH = abs_path
        break

if not FLATCC_PATH:
    # Try to find in system PATH
    FLATCC_PATH = shutil.which('flatcc')

if FLATCC_PATH:
    print(f"Found FlatCC at: {FLATCC_PATH}")
else:
    print("Warning: FlatCC not found. The generator will require --flatcc argument.")

# Collect binaries
binaries = []
if FLATCC_PATH:
    # Bundle FlatCC executable
    binaries.append((FLATCC_PATH, 'bin'))

a = Analysis(
    ['uvrpcc.py'],
    pathex=[spec_root],
    binaries=binaries,
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
    name='uvrpcc',
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

# Use onefile mode to output directly to dist directory
exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='uvrpcc',
    debug=False,
    bootloader_ignore_signals=False,
    strip=True,
    upx=False,
    upx_exclude=[],
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)