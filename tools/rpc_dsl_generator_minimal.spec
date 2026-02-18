# UVRPC DSL Code Generator
# PyInstaller build configuration - Minimal size
# Usage: pyinstaller --clean rpc_dsl_generator_minimal.spec

import os
import sys

block_cipher = None

# Get the directory containing this spec file
spec_root = os.path.dirname(os.path.abspath(SPEC))

# Exclude unnecessary modules to reduce size
excludes = [
    # GUI
    'tkinter',
    'PyQt5',
    'PySide2',
    'wx',
    'matplotlib',
    'numpy',
    'pandas',
    'scipy',
    # Testing
    'pytest',
    'unittest',
    'doctest',
    # Database
    'sqlite3',
    'MySQLdb',
    'psycopg2',
    # Network
    'urllib3',
    'requests',
    'httpx',
    # Development
    'IPython',
    'jupyter',
    # Other
    'distutils',
    'setuptools',
    'pip',
]

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
    excludes=excludes,
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
    upx_exclude=[],
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)