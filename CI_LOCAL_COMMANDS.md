# Local CI Commands - Match GitHub Actions Exactly

## Quick Reference

### Before Creating PR

```bash
# Run ALL checks (exactly what CI runs)
just lint-all

# Fix all formatting issues automatically
just fix-fmt

# Run full CI suite locally
just ci
```

### Individual Checks

```bash
# Rust formatting
just fmt-check          # Check only
just fmt                # Fix formatting

# C formatting
just fmt-c-check        # Check only
just fmt-c              # Fix formatting

# Linting
cargo clippy -- -D warnings     # Rust
just lint-c                     # C (clang-tidy)

# Tests
cargo test              # Rust tests
just test-context       # C context switching tests
```

## What's Currently Failing

### 1. Rust Formatting Issues

**Check:**
```bash
just fmt-check
```

**Fix:**
```bash
just fmt
```

**Issues found:**
- `examples/compile_echo.rs` - import ordering

### 2. Clippy Warnings

**Check:**
```bash
cargo clippy -- -D warnings
```

**Issues found:**
- `src/codegen/mod.rs` - empty line after doc comment
- `src/codegen/error.rs` - empty line after doc comment

**Fix:**
Remove empty lines after `/**` doc comments, or use `/*!` for module-level docs.

### 3. C Formatting

**Check:**
```bash
just fmt-c-check
```

**Fix:**
```bash
just fmt-c
```

## Workflow

### Before Every Commit

```bash
# 1. Fix all formatting
just fix-fmt

# 2. Run linting
just lint-all

# 3. Run tests
cargo test
just test-context
```

### Before Creating PR

```bash
# Run complete CI suite
just ci
```

This runs:
- Rust format check
- C format check  
- Clippy linting
- All Rust tests
- C runtime build
- Context switching tests

### Quick Pre-Commit

```bash
just pre-commit
```

This:
- Fixes all formatting
- Runs linting
- Runs tests

## Common Issues

### "empty line after doc comment"

**Problem:**
```rust
/**
 * Doc comment
 */

pub mod foo;  // ← empty line here
```

**Fix:**
```rust
/**
 * Doc comment
 */
pub mod foo;  // ← no empty line
```

**Or use module-level doc:**
```rust
/*!
 * Doc comment for this module
 */

pub mod foo;  // ← empty line OK here
```

### "clang-format needs to be run"

**Fix:**
```bash
just fmt-c
git add runtime/
```

### "Clippy warnings found"

**Most warnings can be auto-fixed:**
```bash
cargo clippy --fix --allow-dirty
git add src/
```

## CI Workflow Mirrors

| CI Job | Local Command |
|--------|---------------|
| `rust-lint` | `cargo clippy -- -D warnings` |
| `rust-format` | `just fmt-check` |
| `c-lint` | `just fmt-c-check` and `just lint-c` |
| `test-linux-x86` | `cargo test && just test-context` |
| `test-macos-arm64` | `cargo test && just test-context` |
| `build-check` | `cargo build && just build-runtime` |

## Tips

**See what would change:**
```bash
cargo fmt -- --check    # Shows Rust diffs
just fmt-c-check        # Shows C files needing format
```

**Fix everything at once:**
```bash
just fix-fmt            # Formats Rust + C
cargo clippy --fix      # Fixes most Clippy issues
```

**Run subset of tests:**
```bash
cargo test --lib        # Only library tests
cargo test codegen      # Only codegen tests
just test-context       # Only C tests
```

## Summary

**The golden rule:** If `just ci` passes locally, CI will pass on GitHub!

All commands match CI exactly - no surprises.
