# Justfile for Cem compiler
# Install just: brew install just
# Run: just build, just test, etc.

# LLVM configuration
llvm_prefix := "/opt/homebrew/opt/llvm@18"
library_path := "/opt/homebrew/lib"

# Set environment variables for all commands
export LLVM_SYS_180_PREFIX := llvm_prefix
export LIBRARY_PATH := library_path

# Default recipe (runs when you just type 'just')
default:
    @just --list

# Build the project
build:
    cargo build

# Build in release mode
release:
    cargo build --release

# Run all tests
test:
    cargo test

# Run only library tests
test-lib:
    cargo test --lib

# Run only codegen tests
test-codegen:
    cargo test --lib codegen

# Run integration tests
test-integration:
    cargo test --test '*'

# Check code without building
check:
    cargo check

# Run clippy linter
lint:
    cargo clippy -- -D warnings

# Format code
fmt:
    cargo fmt

# Clean build artifacts
clean:
    cargo clean

# Run the compiler (when main is implemented)
run *ARGS:
    cargo run -- {{ARGS}}

# Build and show LLVM IR for a test file
show-ir FILE:
    cargo run -- --emit-llvm {{FILE}}

# Watch for changes and rebuild
watch:
    cargo watch -x build

# Install development dependencies
install-deps:
    @echo "Installing development dependencies..."
    brew install llvm@18 zstd just
    cargo install cargo-watch

# Show LLVM configuration
show-llvm:
    @echo "LLVM_SYS_180_PREFIX: {{llvm_prefix}}"
    @echo "LIBRARY_PATH: {{library_path}}"
    @{{llvm_prefix}}/bin/llvm-config --version

# Build documentation
docs:
    cargo doc --no-deps --open

# Run benchmarks (when implemented)
bench:
    cargo bench

# Full CI check (what CI will run)
ci: fmt lint test
    @echo "✅ All CI checks passed!"
