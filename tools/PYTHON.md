# UVRPC Python Tooling

This directory contains Python-based tooling for UVRPC.

## Installation with uv

```bash
# Install uv
curl -LsSf https://astral.sh/uv/install.sh | sh

# Install development dependencies
uv sync --dev

# Install runtime dependencies only
uv sync
```

## Development Workflow

```bash
# Sync dependencies
uv sync

# Run code generator
uv run python tools/rpc_dsl_generator_with_flatcc.py schema/rpc_api.fbs -o generated

# Build standalone executable
uv run pyinstaller --onefile tools/rpc_dsl_generator_with_flatcc.spec

# Install for development
uv pip install -e .
```

## Building the Generator

```bash
# Standard build
uv run pyinstaller --onefile --clean tools/rpc_dsl_generator_with_flatcc.spec

# With bundled FlatCC
uv run python tools/build_generator_with_flatcc.sh
```

## Virtual Environments

```bash
# Create virtual environment
uv venv

# Activate
source .venv/bin/activate  # Linux/macOS
.venv\Scripts\activate  # Windows

# Install dependencies
uv pip install -r requirements-dev.txt

# Run tools
python tools/rpc_dsl_generator_with_flatcc.py --help
```

## Package Management

```bash
# Add dependency
uv add jinja2

# Add dev dependency
uv add --dev pyinstaller

# Remove dependency
uv remove jinja2

# Update all dependencies
uv sync --upgrade

# Lock dependencies
uv lock
```

## Scripts

The following scripts are available:

- `tools/rpc_dsl_generator_with_flatcc.py` - Main code generator
- `tools/build_generator_with_flatcc.sh` - Build script
- `tools/build_generator_portable.sh` - Portable build
- `tools/build_generator_static.sh` - Static build

## Requirements

### Development
- Python 3.8+
- uv package manager
- Jinja2 3.1+
- PyInstaller 6.0+

### Runtime (for generated executables)
- None required (Python bundled in executable)

## Troubleshooting

### uv not found
```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
export PATH="$HOME/.local/bin:$PATH"
```

### Virtual environment issues
```bash
# Remove and recreate
rm -rf .venv
uv venv
```

### Permission errors
```bash
# Use --user flag
uv pip install --user -r requirements-dev.txt
```

## CI/CD Integration

```yaml
# GitHub Actions example
- name: Install uv
  run: curl -LsSf https://astral.sh/uv/install.sh | sh

- name: Install dependencies
  run: uv sync --dev

- name: Run generator
  run: uv run python tools/rpc_dsl_generator_with_flatcc.py schema/test.fbs -o generated
```

## See Also

- [uv documentation](https://github.com/astral-sh/uv)
- [PyInstaller docs](https://pyinstaller.org/)
- [Project README](../README_SIMPLIFIED.md)