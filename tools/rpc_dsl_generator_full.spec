# UVRPC DSL Code Generator
# PyInstaller build configuration - Full compatibility with bundled FlatCC
# Usage: pyinstaller --clean rpc_dsl_generator_full.spec

import os
import sys
import shutil

block_cipher = None

# Get the directory containing this spec file
spec_root = os.path.dirname(os.path.abspath(SPEC))

# Find and bundle FlatCC
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
    FLATCC_PATH = shutil.which('flatcc')

# Collect binaries
binaries = []
if FLATCC_PATH:
    binaries.append((FLATCC_PATH, 'bin'))

# Find and bundle system libraries for compatibility
system_libs = [
    'libz.so.1',
    'libssl.so.1.1',
    'libcrypto.so.1.1',
    'libffi.so.7',
    'liblzma.so.5',
    'libbz2.so.1.0',
]

for lib in system_libs:
    lib_path = shutil.which(lib)
    if lib_path and os.path.exists(lib_path):
        binaries.append((lib_path, 'lib'))

a = Analysis(
    ['rpc_dsl_generator_with_flatcc.py'],
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
    name='uvrpc-gen',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,  # Don't strip for maximum compatibility
    upx=False,  # Don't compress for maximum compatibility
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)