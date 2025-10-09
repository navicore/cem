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

The project is configured to work with plain `cargo` commands thanks to `.cargo/config.toml`:

```bash
# Build
cargo build

# Test
cargo test

# Release build
cargo build --release
```

## Build Configuration

The LLVM paths are configured in two places:

1. **`.cargo/config.toml`**: Makes `cargo` commands work out of the box
   - Sets `LLVM_SYS_180_PREFIX` to point to LLVM 18
   - Adds library search paths for linking

2. **`justfile`**: Makes `just` commands work
   - Exports the same environment variables
   - Provides convenient aliases for common tasks

## Troubleshooting

### "No suitable version of LLVM was found"

**Solution**: Install LLVM 18 and ensure the paths in `.cargo/config.toml` match your installation:

```bash
# Find your LLVM installation
brew --prefix llvm@18   # macOS
which llvm-config-18    # Linux

# Update .cargo/config.toml with the correct path
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
