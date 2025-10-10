# Justfile for Cem compiler
# Install just: brew install just (or cargo install just)
# Run: just build, just test, etc.

# No LLVM environment variables needed!
# We generate LLVM IR as text and call clang directly.
# Works with any LLVM version installed on the system.

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
    @if [ "{{os()}}" = "macos" ]; then \
        echo "macOS: Installing via Homebrew..."; \
        brew install llvm clang just; \
    else \
        echo "Linux: Install LLVM/clang and just using your package manager:"; \
        echo "  Ubuntu/Debian: sudo apt-get install llvm clang"; \
        echo "  Fedora/Rocky: sudo dnf install llvm clang"; \
        echo "  Arch: sudo pacman -S llvm clang"; \
    fi
    cargo install cargo-watch

# Show LLVM configuration
show-llvm:
    @echo "Checking LLVM installation..."
    @if command -v clang >/dev/null 2>&1; then \
        echo "clang version:"; \
        clang --version | head -1; \
    else \
        echo "clang not found!"; \
    fi
    @if command -v llvm-config >/dev/null 2>&1; then \
        echo "LLVM version:"; \
        llvm-config --version; \
        echo "LLVM prefix:"; \
        llvm-config --prefix; \
    else \
        echo "llvm-config not found (but not required)"; \
    fi

# Build documentation
docs:
    cargo doc --no-deps --open

# Run benchmarks (when implemented)
bench:
    cargo bench

# Full CI check (what CI will run)
ci: fmt lint test
    @echo "✅ All CI checks passed!"

# Build the C runtime library
build-runtime:
    @echo "Building C runtime library..."
    cd runtime && clang -Wall -Wextra -std=c11 -g -O2 -c stack.c -o stack.o
    cd runtime && ar rcs libcem_runtime.a stack.o
    @echo "✅ Built runtime/libcem_runtime.a"

# Build runtime test program
test-runtime: build-runtime
    @echo "Building runtime test..."
    cd runtime && clang -Wall -Wextra -std=c11 -g test_runtime.c -L. -lcem_runtime -o test_runtime
    cd runtime && ./test_runtime
    @echo "✅ Runtime tests passed"

# Clean runtime build artifacts
clean-runtime:
    rm -f runtime/*.o runtime/*.a runtime/test_runtime
    @echo "Cleaned runtime artifacts"
