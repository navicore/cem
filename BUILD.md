# Building Cem

This document explains how to build the Cem compiler.

## Architecture Note

Cem generates LLVM IR as text and invokes `clang` as a subprocess, rather than using Rust FFI bindings. This means:
- ✅ Works with **any LLVM version** (18, 19, 20, 21+)
- ✅ No complex FFI dependencies (no inkwell, no llvm-sys)
- ✅ Simpler setup and maintenance

See `docs/LLVM_TEXT_IR.md` for the full rationale.

## Prerequisites

You need:
1. **Rust toolchain** (install from https://rustup.rs)
2. **clang** (LLVM's C compiler) - any recent version works
3. **just** (optional, for convenient commands)

### macOS (Homebrew)

```bash
# Install LLVM/clang (any version works, 18+ recommended)
brew install llvm

# Optional: Install just for easier commands
brew install just
```

### Linux

```bash
# Ubuntu/Debian
sudo apt-get install clang

# Fedora/Rocky/RHEL
sudo dnf install clang

# Arch
sudo pacman -S clang
```

**Note**: Most Linux distributions come with clang pre-installed or available in standard repos.

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

```bash
# Build
cargo build

# Run tests
cargo test

# Build in release mode
cargo build --release
```

**No environment variables needed!** The project automatically detects `clang` on your PATH.

## Verifying Your Setup

```bash
# Check LLVM/clang installation
just show-llvm

# Or manually:
clang --version  # Should show any recent version (18+, 19+, etc.)
```

## Troubleshooting

### "clang: command not found"

**Solution**: Install LLVM/clang for your platform:

```bash
# macOS
brew install llvm

# Ubuntu/Debian
sudo apt-get install clang

# Fedora/Rocky
sudo dnf install clang

# Arch
sudo pacman -S clang
```

### Integration test failures

If integration tests fail with compilation errors, ensure:
1. `clang` is in your PATH: `which clang`
2. You can compile C code: `echo 'int main(){}' | clang -x c -`

### Runtime compilation warnings

You may see warnings like:
```
warning: overriding the module target triple with x86_64-...
```

These are harmless and can be ignored. They're due to LLVM's target detection.

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

Install the `rust-analyzer` extension. It should work out of the box.

### CLion / IntelliJ IDEA

The Rust plugin should automatically detect the Cargo configuration.

## Platform Support

Cem builds and runs on:
- ✅ **macOS** (Intel and Apple Silicon)
- ✅ **Linux** (Ubuntu, Debian, Fedora, Rocky, Arch, etc.)
- ✅ **Any platform** with Rust + clang installed

The text IR approach ensures LLVM version compatibility is never an issue.

## Next Steps

After successful build, see:
- `README.md` - Project overview and language features
- `docs/LLVM_TEXT_IR.md` - Why we use text IR instead of FFI
- `docs/SELF_HOSTING.md` - Long-term vision
- `stdlib/` - Standard library implementation
