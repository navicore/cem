# Building Cem

This document explains how to build the Cem compiler.

## Prerequisites

### macOS (Homebrew)

```bash
# Install LLVM 18 (required for inkwell)
brew install llvm@18

# Install zstd (required for linking)
brew install zstd

# Optional: Install just for easier commands
brew install just
```

### Linux

```bash
# Ubuntu/Debian
sudo apt-get install llvm-18 llvm-18-dev libzstd-dev

# Fedora
sudo dnf install llvm18 llvm18-devel libzstd-devel

# Arch
sudo pacman -S llvm18 zstd
```

## Building

### Option 1: Using `just` (Recommended)

The `justfile` handles LLVM paths automatically for macOS:

```bash
# Build the project
just build

# Run tests
just test

# Run only codegen tests
just test-codegen

# Build in release mode
just release

# Show all available commands
just
```

### Option 2: Using `cargo` directly

Set the LLVM path first:

```bash
# macOS (Homebrew)
export LLVM_SYS_180_PREFIX=/opt/homebrew/opt/llvm@18
export LIBRARY_PATH=/opt/homebrew/lib

# Linux (Ubuntu/Debian)
export LLVM_SYS_180_PREFIX=/usr/lib/llvm-18

# Then use cargo
cargo build
cargo test
cargo build --release
```

**Tip**: Add the export commands to your shell profile (`~/.zshrc` or `~/.bashrc`) to avoid setting them every time.

## Build Configuration

The LLVM paths must be set via environment variables:

1. **`LLVM_SYS_180_PREFIX`**: Path to your LLVM 18 installation (required)
2. **`LIBRARY_PATH`**: Library search path for linking dependencies like zstd

The `justfile` sets these automatically for macOS Homebrew users. For other platforms or custom LLVM installations, set them manually before running `cargo`.

## Troubleshooting

### "No suitable version of LLVM was found"

**Solution**: Install LLVM 18 and set `LLVM_SYS_180_PREFIX`:

```bash
# Find your LLVM installation
brew --prefix llvm@18   # macOS
which llvm-config-18    # Linux

# Set the environment variable
export LLVM_SYS_180_PREFIX=$(brew --prefix llvm@18)  # macOS
export LLVM_SYS_180_PREFIX=/usr/lib/llvm-18          # Linux
```

### "library 'zstd' not found"

**Solution**: Install zstd and ensure it's in your library path:

```bash
# macOS
brew install zstd

# Linux
sudo apt-get install libzstd-dev  # Debian/Ubuntu
sudo dnf install libzstd-devel     # Fedora
```

### Test failures

If tests fail with linking errors, ensure `LIBRARY_PATH` is set:

```bash
# macOS
export LIBRARY_PATH="/opt/homebrew/lib:$LIBRARY_PATH"

# Or add to your shell profile (~/.zshrc, ~/.bashrc)
echo 'export LIBRARY_PATH="/opt/homebrew/lib:$LIBRARY_PATH"' >> ~/.zshrc
```

## Verifying Your Setup

```bash
# Check LLVM installation
just show-llvm

# Or manually:
/opt/homebrew/opt/llvm@18/bin/llvm-config --version  # Should show 18.x.x
```

## Development Workflow

```bash
# Watch for changes and rebuild automatically
just watch  # Requires: cargo install cargo-watch

# Run linter
just lint

# Format code
just fmt

# Full CI check (format + lint + test)
just ci
```

## IDE Setup

### VS Code

Install the `rust-analyzer` extension. It should automatically pick up the settings from `.cargo/config.toml`.

### CLion / IntelliJ IDEA

The Rust plugin should automatically detect the Cargo configuration.

## Next Steps

After successful build, see:
- `README.md` - Project overview and language features
- `docs/SELF_HOSTING.md` - Long-term vision
- `stdlib/` - Standard library implementation
