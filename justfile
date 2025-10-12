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
    rm -f *.o *.ll echo hello_io test_call test_dbg test_nested_if_debug
    @echo "✅ Cleaned build artifacts"

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
    cd runtime && clang -Wall -Wextra -std=c11 -g -O2 -c context.c -o context.o
    cd runtime && clang -Wall -Wextra -std=c11 -g -O2 -c scheduler.c -o scheduler.o
    cd runtime && clang -Wall -Wextra -std=c11 -g -O2 -c io.c -o io.o
    cd runtime && clang -Wall -Wextra -std=c11 -g -O2 -c stack_mgmt.c -o stack_mgmt.o
    cd runtime && clang -g -O2 -c context_arm64.s -o context_arm64.o
    cd runtime && ar rcs libcem_runtime.a stack.o context.o context_arm64.o scheduler.o io.o stack_mgmt.o
    @echo "✅ Built runtime/libcem_runtime.a"

# Build runtime test program
test-runtime: build-runtime
    @echo "Building runtime test..."
    cd runtime && clang -Wall -Wextra -std=c11 -g test_runtime.c -L. -lcem_runtime -o test_runtime
    cd runtime && ./test_runtime
    @echo "✅ Runtime tests passed"

# Test scheduler infrastructure
test-scheduler: build-runtime
    @echo "Building scheduler tests..."
    cd runtime && clang -Wall -Wextra -std=c11 -g test_scheduler.c -L. -lcem_runtime -o test_scheduler
    cd runtime && ./test_scheduler
    @echo "✅ Scheduler tests passed"

# Test context switching implementation
test-context: build-runtime
    @echo "Building context switching tests..."
    clang -Wall -Wextra -std=c11 -g tests/test_context.c -Lruntime -lcem_runtime -o tests/test_context
    ./tests/test_context
    @echo "✅ Context switching tests passed"

# Test cleanup handler infrastructure
test-cleanup: build-runtime
    @echo "Building cleanup handler tests..."
    clang -Wall -Wextra -std=c11 -g tests/test_cleanup.c -Lruntime -lcem_runtime -o tests/test_cleanup
    ./tests/test_cleanup
    @echo "✅ Cleanup handler tests passed"

# Test I/O cleanup on strand termination
test-io-cleanup: build-runtime
    @echo "Building I/O cleanup tests..."
    clang -Wall -Wextra -std=c11 -g tests/test_io_cleanup.c -Lruntime -lcem_runtime -o tests/test_io_cleanup
    ./tests/test_io_cleanup
    @echo "✅ I/O cleanup tests passed"

# Test dynamic stack growth (Phase 3)
test-stack-growth: build-runtime
    @echo "Building stack growth stress tests..."
    clang -Wall -Wextra -std=c11 -g tests/test_stack_growth.c -Lruntime -lcem_runtime -o tests/test_stack_growth
    ./tests/test_stack_growth
    @echo "✅ Stack growth stress tests passed"

# Run all runtime tests (Phase 3)
test-all-runtime: test-runtime test-scheduler test-context test-cleanup test-io-cleanup test-stack-growth
    @echo ""
    @echo "🎉 All runtime tests passed!"

# Clean runtime build artifacts
clean-runtime:
    rm -f runtime/*.o runtime/*.a runtime/test_runtime runtime/test_scheduler
    rm -f tests/test_context tests/test_cleanup tests/test_io_cleanup
    @echo "Cleaned runtime artifacts"
