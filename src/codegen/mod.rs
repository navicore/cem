/**
LLVM IR Code Generation via Text

This module generates LLVM IR as text (.ll files) and invokes clang
to produce executables. This approach is simpler and more portable than
using FFI bindings (inkwell).

See docs/LLVM_TEXT_IR.md for design rationale.

## Architecture

The code generator walks the AST and emits LLVM IR text:
- Words → Functions
- Literals → push_int/push_bool/push_string calls
- Word calls → Function calls
- Primitives → Runtime function calls

Example output:

```llvm
define ptr @square(ptr %stack) {
entry:
  %0 = call ptr @dup(ptr %stack)
  %1 = call ptr @multiply(ptr %0)
  ret ptr %1
}
```
*/

pub mod error;
pub mod ir;
pub mod linker;

pub use error::{CodegenError, CodegenResult};
pub use ir::IRGenerator;
pub use linker::{compile_to_object, link_program};

use crate::ast::{Expr, Program, WordDef};
use std::fmt::Write as _;
use std::process::Command;

/// Main code generator
pub struct CodeGen {
    output: String,
    temp_counter: usize,
    current_block: String,  // Track the current basic block label we're emitting into
}

impl CodeGen {
    /// Create a new code generator
    pub fn new() -> Self {
        CodeGen {
            output: String::new(),
            temp_counter: 0,
            current_block: "entry".to_string(),
        }
    }

    /// Generate a fresh temporary variable name (without % prefix)
    fn fresh_temp(&mut self) -> String {
        let name = format!("{}", self.temp_counter);
        self.temp_counter += 1;
        name
    }

    /// Escape a string for LLVM IR string literals
    /// LLVM IR requires hex escaping for non-printable characters
    fn escape_llvm_string(s: &str) -> String {
        let mut result = String::new();
        for ch in s.chars() {
            match ch {
                // Printable ASCII except backslash and quotes
                ' '..='!' | '#'..='[' | ']'..='~' => result.push(ch),
                // Escape backslash
                '\\' => result.push_str(r"\\"),
                // Escape quote
                '"' => result.push_str(r#"\""#),
                // All other characters as hex escapes
                _ => {
                    for byte in ch.to_string().as_bytes() {
                        result.push_str(&format!(r"\{:02X}", byte));
                    }
                }
            }
        }
        result
    }

    /// Compile a complete program to LLVM IR
    pub fn compile_program(&mut self, program: &Program) -> CodegenResult<String> {
        self.compile_program_with_main(program, None)
    }

    /// Compile a complete program to LLVM IR with optional main() function
    ///
    /// # Arguments
    /// * `program` - The AST program to compile
    /// * `entry_word` - Optional name of word to call from main(). If None, no main() is generated.
    ///                  If Some("word_name"), generates main() that calls that word and prints result.
    pub fn compile_program_with_main(&mut self, program: &Program, entry_word: Option<&str>) -> CodegenResult<String> {
        // Emit module header with target triple to match host platform
        writeln!(&mut self.output, "; Cem Compiler - Generated LLVM IR")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output)
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;

        // Set target triple to match clang's default (prevents warnings)
        // We query clang directly to get the exact triple it expects
        let target_triple = Self::get_target_triple().map_err(|e| {
            CodegenError::LinkerError {
                message: format!("Failed to detect target triple from clang: {}", e),
            }
        })?;
        writeln!(&mut self.output, "target triple = \"{}\"", target_triple)
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output)
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;

        // Declare runtime functions
        self.emit_runtime_declarations()?;

        // Emit all word definitions
        for word in &program.word_defs {
            self.compile_word(word)?;
        }

        // Generate main() if requested
        if let Some(word_name) = entry_word {
            self.emit_main_function(word_name)?;
        }

        Ok(self.output.clone())
    }
    
    /// Get the target triple by querying clang
    /// 
    /// This ensures we match clang's exact default target, avoiding warnings
    /// about target triple mismatches when compiling LLVM IR.
    /// 
    /// # Returns
    /// 
    /// The target triple string (e.g., "x86_64-apple-darwin" or "x86_64-redhat-linux-gnu")
    /// 
    /// # Errors
    /// 
    /// Returns an error if clang is not found or fails to report its target
    fn get_target_triple() -> Result<String, std::io::Error> {
        let output = Command::new("clang")
            .arg("-dumpmachine")
            .output()?;
            
        if output.status.success() {
            Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
        } else {
            Err(std::io::Error::new(
                std::io::ErrorKind::Other,
                "clang -dumpmachine failed"
            ))
        }
    }

    /// Emit declarations for all runtime functions
    fn emit_runtime_declarations(&mut self) -> CodegenResult<()> {
        writeln!(&mut self.output, "; Runtime function declarations")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;

        // Stack operations (ptr -> ptr)
        for func in &["dup", "drop", "swap", "over", "rot"] {
            writeln!(&mut self.output, "declare ptr @{}(ptr)", func)
                .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        }

        // Arithmetic (ptr -> ptr)
        for func in &["add", "subtract", "multiply", "divide"] {
            writeln!(&mut self.output, "declare ptr @{}(ptr)", func)
                .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        }

        // Comparisons (ptr -> ptr)
        for func in &["less_than", "greater_than", "equal"] {
            writeln!(&mut self.output, "declare ptr @{}(ptr)", func)
                .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        }

        // Push operations
        writeln!(&mut self.output, "declare ptr @push_int(ptr, i64)")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output, "declare ptr @push_bool(ptr, i1)")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output, "declare ptr @push_string(ptr, ptr)")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;

        // Utility functions
        writeln!(&mut self.output, "declare void @print_stack(ptr)")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output, "declare void @free_stack(ptr)")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;

        writeln!(&mut self.output)
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        Ok(())
    }

    /// Emit a main() function that calls an entry word
    ///
    /// Generates:
    /// ```llvm
    /// define i32 @main() {
    /// entry:
    ///   %stack = call ptr @entry_word(ptr null)
    ///   call void @print_stack(ptr %stack)
    ///   call void @free_stack(ptr %stack)
    ///   ret i32 0
    /// }
    /// ```
    fn emit_main_function(&mut self, entry_word: &str) -> CodegenResult<()> {
        writeln!(&mut self.output, "; Main function")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output, "define i32 @main() {{")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output, "entry:")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output, "  %stack = call ptr @{}(ptr null)", entry_word)
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output, "  call void @print_stack(ptr %stack)")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output, "  call void @free_stack(ptr %stack)")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output, "  ret i32 0")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output, "}}")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output)
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        Ok(())
    }

    /// Compile a word definition to LLVM function
    fn compile_word(&mut self, word: &WordDef) -> CodegenResult<()> {
        self.temp_counter = 0; // Reset for each function

        writeln!(&mut self.output, "define ptr @{}(ptr %stack) {{", word.name)
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output, "entry:")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;

        let mut stack_var = "stack".to_string();

        // Compile all expressions except possibly the last
        let body_len = word.body.len();
        for (i, expr) in word.body.iter().enumerate() {
            let is_tail = i == body_len - 1;
            stack_var = self.compile_expr_with_context(expr, &stack_var, is_tail)?;
        }

        // Return final stack
        writeln!(&mut self.output, "  ret ptr %{}", stack_var)
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;

        writeln!(&mut self.output, "}}")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output)
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;

        Ok(())
    }

    /// Compile a quotation in a branch (then/else)
    /// Returns the final stack variable name
    /// Compile a branch quotation and return (result_var, ends_with_musttail)
    fn compile_branch_quotation(&mut self, quot: &Expr, initial_stack: &str) -> CodegenResult<(String, bool)> {
        match quot {
            Expr::Quotation(exprs) => {
                let mut stack_var = initial_stack.to_string();
                let len = exprs.len();
                let mut ends_with_musttail = false;

                for (i, expr) in exprs.iter().enumerate() {
                    let is_tail = i == len - 1;  // Track tail position in branch
                    stack_var = self.compile_expr_with_context(expr, &stack_var, is_tail)?;

                    // Check if this is a tail call (last expr that is a WordCall)
                    if is_tail {
                        if let Expr::WordCall(_) = expr {
                            ends_with_musttail = true;
                        }
                    }
                }
                Ok((stack_var, ends_with_musttail))
            }
            _ => Err(CodegenError::InternalError(
                "If branches must be quotations".to_string()
            ))
        }
    }

    /// Compile a single expression with tail-call context
    fn compile_expr_with_context(&mut self, expr: &Expr, stack: &str, in_tail_position: bool) -> CodegenResult<String> {
        match expr {
            // Tail-call optimization: if in tail position and calling a word, use musttail
            Expr::WordCall(name) if in_tail_position => {
                let result = self.fresh_temp();
                writeln!(
                    &mut self.output,
                    "  %{} = musttail call ptr @{}(ptr %{})",
                    result, name, stack
                )
                .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                Ok(result)
            }
            // Otherwise, delegate to normal compile_expr
            _ => self.compile_expr(expr, stack)
        }
    }

    /// Compile a single expression, returning the new stack variable name
    fn compile_expr(&mut self, expr: &Expr, stack: &str) -> CodegenResult<String> {
        match expr {
            Expr::IntLit(n) => {
                let result = self.fresh_temp();
                writeln!(
                    &mut self.output,
                    "  %{} = call ptr @push_int(ptr %{}, i64 {})",
                    result, stack, n
                )
                .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                Ok(result)
            }

            Expr::BoolLit(b) => {
                let result = self.fresh_temp();
                let value = if *b { 1 } else { 0 };
                writeln!(
                    &mut self.output,
                    "  %{} = call ptr @push_bool(ptr %{}, i1 {})",
                    result, stack, value
                )
                .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                Ok(result)
            }

            Expr::StringLit(s) => {
                // Create global string constant
                let str_global = format!("@.str.{}", self.temp_counter);
                let escaped = Self::escape_llvm_string(s);
                // Length is original byte count - escaping is just text representation.
                // E.g., "a\"b" is 3 bytes even though we write it as 5 chars in IR text.
                // UTF-8 chars like "😀" (4 bytes) escape to "\F0\9F\98\80" but still represent 4 bytes.
                let str_len = s.as_bytes().len() + 1; // +1 for null terminator

                // Emit global at top of file (we'll prepend it later)
                let global_decl = format!(
                    "{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"\n",
                    str_global, str_len, escaped
                );
                self.output = global_decl + &self.output;

                let result = self.fresh_temp();
                let ptr_temp = self.fresh_temp();

                writeln!(
                    &mut self.output,
                    "  %{} = getelementptr inbounds [{} x i8], ptr {}, i32 0, i32 0",
                    ptr_temp, str_len, str_global
                )
                .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                writeln!(
                    &mut self.output,
                    "  %{} = call ptr @push_string(ptr %{}, ptr %{})",
                    result, stack, ptr_temp
                )
                .map_err(|e| CodegenError::InternalError(e.to_string()))?;

                Ok(result)
            }

            Expr::WordCall(name) => {
                let result = self.fresh_temp();
                writeln!(
                    &mut self.output,
                    "  %{} = call ptr @{}(ptr %{})",
                    result, name, stack
                )
                .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                Ok(result)
            }

            Expr::Quotation(_) => Err(CodegenError::Unimplemented {
                feature: "quotations".to_string(),
            }),

            Expr::Match { .. } => Err(CodegenError::Unimplemented {
                feature: "pattern matching".to_string(),
            }),

            Expr::If { then_branch, else_branch } => {
                // Stack top must be a Bool
                // Strategy: extract bool, branch to then/else, both produce same stack effect

                // Generate unique labels
                let then_label = format!("then_{}", self.temp_counter);
                let else_label = format!("else_{}", self.temp_counter);
                let merge_label = format!("merge_{}", self.temp_counter);
                self.temp_counter += 1;

                // Extract boolean value from stack top
                // StackCell C layout (from runtime/stack.h):
                //   - tag: i32 at offset 0 (4 bytes)
                //   - padding: 4 bytes (for union alignment)
                //   - value union at offset 8 (16 bytes total - largest member is variant struct)
                //   - next: ptr at offset 24 (8 bytes)
                // LLVM struct: { i32, [4 x i8], [16 x i8], ptr } = 32 bytes

                // Get bool value from union at offset 8 (field index 2)
                // Bool is stored as i8 in the first byte of the 16-byte union
                let bool_ptr = self.fresh_temp();
                writeln!(&mut self.output, "  %{} = getelementptr inbounds {{ i32, [4 x i8], [16 x i8], ptr }}, ptr %{}, i32 0, i32 2, i32 0", bool_ptr, stack)
                    .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                let bool_val = self.fresh_temp();
                writeln!(&mut self.output, "  %{} = load i8, ptr %{}", bool_val, bool_ptr)
                    .map_err(|e| CodegenError::InternalError(e.to_string()))?;

                // Use fresh temp for cond to avoid collisions in nested ifs
                let cond_var = self.fresh_temp();
                writeln!(&mut self.output, "  %{} = trunc i8 %{} to i1", cond_var, bool_val)
                    .map_err(|e| CodegenError::InternalError(e.to_string()))?;

                // Get rest of stack (next pointer at field index 3)
                let rest_ptr = self.fresh_temp();
                writeln!(&mut self.output, "  %{} = getelementptr inbounds {{ i32, [4 x i8], [16 x i8], ptr }}, ptr %{}, i32 0, i32 3", rest_ptr, stack)
                    .map_err(|e| CodegenError::InternalError(e.to_string()))?;

                // Use fresh temp for rest to avoid collisions in nested ifs
                let rest_var = self.fresh_temp();
                writeln!(&mut self.output, "  %{} = load ptr, ptr %{}", rest_var, rest_ptr)
                    .map_err(|e| CodegenError::InternalError(e.to_string()))?;

                // Branch using the condition variable
                writeln!(&mut self.output, "  br i1 %{}, label %{}, label %{}", cond_var, then_label, else_label)
                    .map_err(|e| CodegenError::InternalError(e.to_string()))?;

                // Then branch
                writeln!(&mut self.output, "{}:", then_label)
                    .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                self.current_block = then_label.clone();
                let (then_stack, then_is_musttail) = self.compile_branch_quotation(then_branch, &rest_var)?;

                // Capture the actual block that will branch to merge (after any nested ifs)
                let then_predecessor = self.current_block.clone();

                // If then branch ends with musttail, emit return instead of branch
                if then_is_musttail {
                    writeln!(&mut self.output, "  ret ptr %{}", then_stack)
                        .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                } else {
                    writeln!(&mut self.output, "  br label %{}", merge_label)
                        .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                }

                // Else branch
                writeln!(&mut self.output, "{}:", else_label)
                    .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                self.current_block = else_label.clone();
                let (else_stack, else_is_musttail) = self.compile_branch_quotation(else_branch, &rest_var)?;

                // Capture the actual block that will branch to merge (after any nested ifs)
                let else_predecessor = self.current_block.clone();

                // If else branch ends with musttail, emit return instead of branch
                if else_is_musttail {
                    writeln!(&mut self.output, "  ret ptr %{}", else_stack)
                        .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                } else {
                    writeln!(&mut self.output, "  br label %{}", merge_label)
                        .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                }

                // Merge point - only if at least one branch doesn't end with musttail
                if !then_is_musttail || !else_is_musttail {
                    writeln!(&mut self.output, "{}:", merge_label)
                        .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                    self.current_block = merge_label.clone();

                    // Build phi node based on which branches contribute
                    let result = self.fresh_temp();
                    if !then_is_musttail && !else_is_musttail {
                        // Both branches merge - use actual predecessors
                        writeln!(&mut self.output, "  %{} = phi ptr [ %{}, %{} ], [ %{}, %{} ]",
                            result, then_stack, then_predecessor, else_stack, else_predecessor)
                            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                    } else if !then_is_musttail {
                        // Only then branch merges (else returned)
                        writeln!(&mut self.output, "  %{} = phi ptr [ %{}, %{} ]",
                            result, then_stack, then_predecessor)
                            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                    } else {
                        // Only else branch merges (then returned)
                        writeln!(&mut self.output, "  %{} = phi ptr [ %{}, %{} ]",
                            result, else_stack, else_predecessor)
                            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
                    }
                    Ok(result)
                } else {
                    // Both branches end with musttail and return - no merge point needed
                    // This is actually unreachable code after the if, so return a dummy value
                    Ok(then_stack) // Won't be used since both branches returned
                }
            }

        }
    }

    /// Get the generated LLVM IR
    pub fn emit_ir(&self) -> String {
        self.output.clone()
    }
}

impl Default for CodeGen {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ast::types::{Effect, StackType, Type};

    #[test]
    fn test_codegen_simple() {
        let mut codegen = CodeGen::new();

        // : five ( -- Int ) 5 ;
        let word = WordDef {
            name: "five".to_string(),
            effect: Effect {
                inputs: StackType::Empty,
                outputs: StackType::Empty.push(Type::Int),
            },
            body: vec![Expr::IntLit(5)],
        };

        let program = Program {
            type_defs: vec![],
            word_defs: vec![word],
        };

        let ir = codegen.compile_program(&program).unwrap();

        assert!(ir.contains("define ptr @five"));
        assert!(ir.contains("call ptr @push_int"));
        assert!(ir.contains("i64 5"));
        assert!(ir.contains("ret ptr"));
        // Should contain target triple
        assert!(ir.contains("target triple"));
    }

    #[test]
    fn test_codegen_word_call() {
        let mut codegen = CodeGen::new();

        // : double ( Int -- Int ) dup + ;
        let word = WordDef {
            name: "double".to_string(),
            effect: Effect {
                inputs: StackType::Empty.push(Type::Int),
                outputs: StackType::Empty.push(Type::Int),
            },
            body: vec![
                Expr::WordCall("dup".to_string()),
                Expr::WordCall("add".to_string()),
            ],
        };

        let program = Program {
            type_defs: vec![],
            word_defs: vec![word],
        };

        let ir = codegen.compile_program(&program).unwrap();

        assert!(ir.contains("@double"));
        assert!(ir.contains("call ptr @dup"));
        assert!(ir.contains("call ptr @add"));
    }

    #[test]
    fn test_get_target_triple() {
        // Test that we can successfully query clang for target triple
        let result = CodeGen::get_target_triple();
        
        assert!(result.is_ok(), "Failed to get target triple from clang");
        
        let triple = result.unwrap();
        
        // Target triple should be non-empty
        assert!(!triple.is_empty(), "Target triple should not be empty");
        
        // Should contain architecture (common patterns)
        assert!(
            triple.contains("x86_64") 
            || triple.contains("aarch64") 
            || triple.contains("arm64")
            || triple.contains("i686"),
            "Target triple should contain a known architecture, got: {}",
            triple
        );
        
        // Should contain OS (common patterns)
        assert!(
            triple.contains("linux") 
            || triple.contains("darwin") 
            || triple.contains("windows")
            || triple.contains("freebsd"),
            "Target triple should contain a known OS, got: {}",
            triple
        );
    }

    #[test]
    fn test_target_triple_in_generated_ir() {
        let mut codegen = CodeGen::new();
        
        let word = WordDef {
            name: "test".to_string(),
            effect: Effect {
                inputs: StackType::Empty,
                outputs: StackType::Empty,
            },
            body: vec![],
        };
        
        let program = Program {
            type_defs: vec![],
            word_defs: vec![word],
        };
        
        let ir = codegen.compile_program(&program).unwrap();
        
        // Verify that target triple is present in the IR
        assert!(ir.contains("target triple = \""), "IR should contain target triple declaration");
        
        // Extract the target triple from IR and verify it matches what clang reports
        let expected_triple = CodeGen::get_target_triple().unwrap();
        assert!(
            ir.contains(&format!("target triple = \"{}\"", expected_triple)),
            "IR should contain the correct target triple: {}",
            expected_triple
        );
    }
}
