# Debug Metadata Implementation Plan

## Goal
Add LLVM debug metadata to enable source-level debugging with LLDB/GDB. This will show Cem source lines instead of IR/assembly when debugging.

## Why Now?
With I/O and CLI coming in future phases, we need to distinguish between:
- **User program bugs** - incorrect Cem code
- **Runtime bugs** - issues in our scheduler/I/O implementation
- **Codegen bugs** - incorrect IR generation

Debug metadata makes this trivial: see exactly which Cem line caused the issue.

## Implementation Tasks

### Phase 1: AST Changes (4 hours)

**Task 1.1: Add SourceLoc struct**
```rust
// src/ast/mod.rs

/// Source code location for debugging and error messages
#[derive(Debug, Clone, PartialEq)]
pub struct SourceLoc {
    pub line: usize,
    pub column: usize,
    pub file: String,
}

impl SourceLoc {
    pub fn new(line: usize, column: usize, file: String) -> Self {
        Self { line, column, file }
    }

    pub fn unknown() -> Self {
        Self {
            line: 0,
            column: 0,
            file: "<unknown>".to_string()
        }
    }
}
```

**Task 1.2: Update Expr to include locations**
```rust
// src/ast/mod.rs

#[derive(Debug, Clone, PartialEq)]
pub enum Expr {
    IntLit(i64, SourceLoc),
    BoolLit(bool, SourceLoc),
    StringLit(String, SourceLoc),
    WordCall(String, SourceLoc),
    Quotation(Vec<Expr>, SourceLoc),
    If {
        then_branch: Box<Expr>,
        else_branch: Box<Expr>,
        loc: SourceLoc,
    },
}

impl Expr {
    /// Get the source location of any expression
    pub fn loc(&self) -> &SourceLoc {
        match self {
            Expr::IntLit(_, loc) => loc,
            Expr::BoolLit(_, loc) => loc,
            Expr::StringLit(_, loc) => loc,
            Expr::WordCall(_, loc) => loc,
            Expr::Quotation(_, loc) => loc,
            Expr::If { loc, .. } => loc,
        }
    }
}
```

**Task 1.3: Update WordDef to include location**
```rust
#[derive(Debug, Clone, PartialEq)]
pub struct WordDef {
    pub name: String,
    pub effect: types::Effect,
    pub body: Vec<Expr>,
    pub loc: SourceLoc,  // Location of word definition
}
```

**Testing**: Update AST tests to include dummy SourceLoc::unknown()

### Phase 2: Parser Changes (6 hours)

**Task 2.1: Track current position in parser**

The parser likely uses a lexer/tokenizer. We need to:
1. Find where tokens are created
2. Add line/column tracking to tokens
3. Pass location info when constructing AST nodes

**Example pattern** (actual implementation depends on parser):
```rust
// Hypothetical parser update
fn parse_int_lit(&mut self) -> Result<Expr> {
    let token = self.consume(TokenKind::Int)?;
    let value = token.value.parse()?;
    let loc = SourceLoc::new(
        token.line,
        token.column,
        self.filename.clone()
    );
    Ok(Expr::IntLit(value, loc))
}
```

**Task 2.2: Update all parse methods**
- parse_word_call
- parse_quotation
- parse_if_expression
- parse_literal (int, bool, string)
- parse_word_definition

**Testing**: Parse sample programs and verify locations are correct

### Phase 3: Codegen Debug Metadata (8 hours)

**Task 3.1: Add debug metadata infrastructure**
```rust
// src/codegen/mod.rs

pub struct CodeGen {
    output: String,
    temp_counter: usize,
    current_block: String,

    // New debug fields
    debug_counter: usize,           // Counter for !N metadata IDs
    current_file: Option<String>,   // Current source file
}

impl CodeGen {
    fn fresh_debug_id(&mut self) -> usize {
        let id = self.debug_counter;
        self.debug_counter += 1;
        id
    }
}
```

**Task 3.2: Emit file and compile unit metadata**
```rust
fn emit_debug_metadata(&mut self, filename: &str) -> CodegenResult<()> {
    // Get absolute path
    let abs_path = std::env::current_dir()
        .map_err(|e| CodegenError::InternalError(e.to_string()))?;
    let abs_file = abs_path.join(filename);

    // Extract directory and filename
    let dir = abs_file.parent()
        .and_then(|p| p.to_str())
        .unwrap_or(".");
    let file = abs_file.file_name()
        .and_then(|f| f.to_str())
        .unwrap_or(filename);

    writeln!(&mut self.output,
        "!llvm.dbg.cu = !{{!0}}"
    ).map_err(|e| CodegenError::InternalError(e.to_string()))?;

    writeln!(&mut self.output,
        "!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: \"Cem Compiler\", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)"
    ).map_err(|e| CodegenError::InternalError(e.to_string()))?;

    writeln!(&mut self.output,
        "!1 = !DIFile(filename: \"{}\", directory: \"{}\")",
        file, dir
    ).map_err(|e| CodegenError::InternalError(e.to_string()))?;

    self.current_file = Some(filename.to_string());
    Ok(())
}
```

**Task 3.3: Emit subprogram metadata for each word**
```rust
fn emit_word_debug_metadata(&mut self, word: &WordDef) -> CodegenResult<usize> {
    let debug_id = self.fresh_debug_id();

    writeln!(&mut self.output,
        "!{} = distinct !DISubprogram(name: \"{}\", scope: !1, file: !1, line: {}, type: !{}, scopeLine: {}, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0)",
        debug_id,
        word.name,
        word.loc.line,
        debug_id + 1,  // Type metadata (we'll create this)
        word.loc.line
    ).map_err(|e| CodegenError::InternalError(e.to_string()))?;

    // Emit function type (simplified - just say it's a function)
    let type_id = self.fresh_debug_id();
    writeln!(&mut self.output,
        "!{} = !DISubroutineType(types: !{})",
        type_id,
        type_id + 1
    ).map_err(|e| CodegenError::InternalError(e.to_string()))?;

    // Empty types array
    let types_id = self.fresh_debug_id();
    writeln!(&mut self.output,
        "!{} = !{{}}",
        types_id
    ).map_err(|e| CodegenError::InternalError(e.to_string()))?;

    Ok(debug_id)
}
```

**Task 3.4: Attach debug info to function definitions**
```rust
fn compile_word_with_debug(&mut self, word: &WordDef) -> CodegenResult<()> {
    // Emit debug metadata
    let subprogram_id = self.emit_word_debug_metadata(word)?;

    // Emit function with debug attribute
    writeln!(&mut self.output,
        "define ptr @{}(ptr %stack) !dbg !{} {{",
        word.name,
        subprogram_id
    ).map_err(|e| CodegenError::InternalError(e.to_string()))?;

    // ... rest of function compilation

    Ok(())
}
```

**Task 3.5: Attach !dbg to each instruction**
```rust
fn compile_expr_with_debug(&mut self, expr: &Expr, stack: &str) -> CodegenResult<String> {
    let result = match expr {
        Expr::IntLit(val, loc) => {
            let temp = self.fresh_temp();
            let loc_id = self.emit_location(loc)?;
            writeln!(&mut self.output,
                "  %{} = call ptr @push_int(ptr %{}, i64 {}), !dbg !{}",
                temp, stack, val, loc_id
            )?;
            temp
        }

        Expr::WordCall(name, loc) => {
            let temp = self.fresh_temp();
            let loc_id = self.emit_location(loc)?;
            writeln!(&mut self.output,
                "  %{} = call ptr @{}(ptr %{}), !dbg !{}",
                temp, name, stack, loc_id
            )?;
            temp
        }

        // ... handle other expr types
    };

    Ok(result)
}

fn emit_location(&mut self, loc: &SourceLoc) -> CodegenResult<usize> {
    let id = self.fresh_debug_id();
    writeln!(&mut self.output,
        "!{} = !DILocation(line: {}, column: {}, scope: !{})",
        id, loc.line, loc.column,
        self.current_subprogram  // Track this in CodeGen state
    ).map_err(|e| CodegenError::InternalError(e.to_string()))?;

    Ok(id)
}
```

**Testing**:
- Verify generated IR includes debug metadata
- Check with `llvm-dis` or manual inspection
- No semantic changes to generated code

### Phase 4: Integration & Testing (4 hours)

**Task 4.1: Update compile_program to emit debug metadata**
```rust
pub fn compile_program_with_debug(&mut self, program: &Program, filename: &str) -> CodegenResult<String> {
    self.emit_debug_metadata(filename)?;

    // Emit word definitions with debug info
    for word in &program.word_defs {
        self.compile_word_with_debug(word)?;
    }

    // ... rest of compilation

    Ok(self.output.clone())
}
```

**Task 4.2: Create test program**
```cem
# test_debug.cem
: add_numbers ( Int Int -- Int )
    +  # Line 3
;

: double ( Int -- Int )
    dup     # Line 7
    +       # Line 8
;

: main ( -- Int )
    5 10        # Line 12
    add_numbers # Line 13
    double      # Line 14
;
```

**Task 4.3: Test with LLDB**
```bash
# Compile with debug info
cargo run -- test_debug.cem main

# Debug
lldb test_debug
(lldb) break test_debug.cem:13
(lldb) run
(lldb) list
# Should show Cem source code!

(lldb) step
# Should step to line 3 (inside add_numbers)

(lldb) bt
# Should show call stack with Cem source locations
```

**Task 4.4: Verify VS Code integration**
- Install CodeLLDB extension
- Set breakpoint in .cem file
- Step through code
- Verify source display works

**Task 4.5: Update error messages**
```rust
// Use SourceLoc in error messages
fn type_error(&self, expected: &Type, got: &Type, loc: &SourceLoc) -> TypeCheckError {
    TypeCheckError::TypeMismatch {
        expected: expected.clone(),
        got: got.clone(),
        location: format("{}:{}:{}", loc.file, loc.line, loc.column),
    }
}
```

### Phase 5: Documentation (2 hours)

**Task 5.1: Update DEBUGGING.md**
- Add "Phase 2 Complete ✅" status
- Document how to use source-level debugging
- Add screenshots/examples of LLDB showing Cem code

**Task 5.2: Add debugging section to main docs**
- Update README with debugging workflow
- Add example debugging session

**Task 5.3: Update BUILD.md**
- Note that `-g` is included by default for debug builds
- Document how to strip debug info for release builds

## Timeline

**Day 1** (8 hours):
- Morning: AST changes (Tasks 1.1-1.3) - 4 hours
- Afternoon: Parser investigation and initial changes (Task 2.1) - 4 hours

**Day 2** (8 hours):
- Morning: Complete parser changes (Task 2.2) - 4 hours
- Afternoon: Debug metadata infrastructure (Tasks 3.1-3.2) - 4 hours

**Day 3** (8 hours):
- Morning: Subprogram and instruction metadata (Tasks 3.3-3.5) - 6 hours
- Afternoon: Integration and testing (Tasks 4.1-4.3) - 2 hours

**Day 4** (4 hours):
- Morning: VS Code testing and error messages (Tasks 4.4-4.5) - 2 hours
- Afternoon: Documentation (Tasks 5.1-5.3) - 2 hours

**Total**: ~28 hours (~3.5 days of focused work)

## Success Criteria

✅ LLDB shows Cem source lines, not IR
✅ Breakpoints work on .cem files: `break program.cem:42`
✅ Step through Cem code line-by-line
✅ Call stack shows Cem function names and locations
✅ VS Code debugger integration works
✅ Error messages show accurate source locations
✅ All existing tests still pass
✅ Generated binaries still run correctly

## References

- **LLVM Debug Info Format**: https://llvm.org/docs/SourceLevelDebugging.html
- **DWARF Spec**: http://dwarfstd.org/
- **Example (Rust)**: https://github.com/rust-lang/rust/blob/master/compiler/rustc_codegen_llvm/src/debuginfo/
- **Example (Zig)**: https://github.com/ziglang/zig/blob/master/src/codegen/llvm/bindings.zig

## Dependencies

- Parser must be able to track line/column (investigate current parser first)
- No new external dependencies needed
- LLVM version independence maintained (debug metadata is stable)

## Risks & Mitigations

**Risk**: Parser doesn't track positions
- **Mitigation**: Add position tracking (standard parser feature)

**Risk**: Debug metadata increases binary size
- **Mitigation**: Strip in release builds (`-Wl,-S` flag)

**Risk**: Metadata syntax errors break compilation
- **Mitigation**: Extensive testing, follow LLVM spec exactly

**Risk**: Slowdown in compilation
- **Mitigation**: Metadata emission is simple string formatting (~5% overhead)

---

**Status**: Ready to implement
**Priority**: High (needed for I/O development)
**Estimated effort**: 3.5 days
**Dependencies**: None (can start immediately)
