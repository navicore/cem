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

/// Main code generator
pub struct CodeGen {
    output: String,
    temp_counter: usize,
}

impl CodeGen {
    /// Create a new code generator
    pub fn new() -> Self {
        CodeGen {
            output: String::new(),
            temp_counter: 0,
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
        // Emit module header
        writeln!(&mut self.output, "; Cem Compiler - Generated LLVM IR")
            .map_err(|e| CodegenError::InternalError(e.to_string()))?;
        writeln!(&mut self.output).unwrap();

        // Declare runtime functions
        self.emit_runtime_declarations()?;

        // Emit all word definitions
        for word in &program.word_defs {
            self.compile_word(word)?;
        }

        Ok(self.output.clone())
    }

    /// Emit declarations for all runtime functions
    fn emit_runtime_declarations(&mut self) -> CodegenResult<()> {
        writeln!(&mut self.output, "; Runtime function declarations").unwrap();

        // Stack operations (ptr -> ptr)
        for func in &["dup", "drop", "swap", "over", "rot"] {
            writeln!(&mut self.output, "declare ptr @{}(ptr)", func).unwrap();
        }

        // Arithmetic (ptr -> ptr)
        for func in &["add", "subtract", "multiply", "divide"] {
            writeln!(&mut self.output, "declare ptr @{}(ptr)", func).unwrap();
        }

        // Comparisons (ptr -> ptr)
        for func in &["less_than", "greater_than", "equal"] {
            writeln!(&mut self.output, "declare ptr @{}(ptr)", func).unwrap();
        }

        // Push operations
        writeln!(&mut self.output, "declare ptr @push_int(ptr, i64)").unwrap();
        writeln!(&mut self.output, "declare ptr @push_bool(ptr, i1)").unwrap();
        writeln!(&mut self.output, "declare ptr @push_string(ptr, ptr)").unwrap();

        writeln!(&mut self.output).unwrap();
        Ok(())
    }

    /// Compile a word definition to LLVM function
    fn compile_word(&mut self, word: &WordDef) -> CodegenResult<()> {
        self.temp_counter = 0; // Reset for each function

        writeln!(&mut self.output, "define ptr @{}(ptr %stack) {{", word.name).unwrap();
        writeln!(&mut self.output, "entry:").unwrap();

        let mut stack_var = "stack".to_string();

        // Compile each expression in the body
        for expr in &word.body {
            stack_var = self.compile_expr(expr, &stack_var)?;
        }

        // Return final stack
        writeln!(&mut self.output, "  ret ptr %{}", stack_var).unwrap();

        writeln!(&mut self.output, "}}").unwrap();
        writeln!(&mut self.output).unwrap();

        Ok(())
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
                .unwrap();
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
                .unwrap();
                Ok(result)
            }

            Expr::StringLit(s) => {
                // Create global string constant
                let str_global = format!("@.str.{}", self.temp_counter);
                let escaped = Self::escape_llvm_string(s);
                // Calculate actual byte length after escaping
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
                .unwrap();
                writeln!(
                    &mut self.output,
                    "  %{} = call ptr @push_string(ptr %{}, ptr %{})",
                    result, stack, ptr_temp
                )
                .unwrap();

                Ok(result)
            }

            Expr::WordCall(name) => {
                let result = self.fresh_temp();
                writeln!(
                    &mut self.output,
                    "  %{} = call ptr @{}(ptr %{})",
                    result, name, stack
                )
                .unwrap();
                Ok(result)
            }

            Expr::Quotation(_) => Err(CodegenError::Unimplemented {
                feature: "quotations".to_string(),
            }),

            Expr::Match { .. } => Err(CodegenError::Unimplemented {
                feature: "pattern matching".to_string(),
            }),

            Expr::If { .. } => Err(CodegenError::Unimplemented {
                feature: "if expressions".to_string(),
            }),

            Expr::While { .. } => Err(CodegenError::Unimplemented {
                feature: "while loops".to_string(),
            }),
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
}
