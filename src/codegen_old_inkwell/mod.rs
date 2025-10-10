/**
LLVM Code Generation for Cem

This module compiles type-checked Cem programs to LLVM IR.

## Architecture

Cem uses a **stack machine** architecture for its runtime:
- Stack is represented as a linked list of heap-allocated cells
- Each stack cell contains a tagged union (value + type tag)
- Stack operations are function calls to runtime primitives

## Stack Representation

```c
// Runtime representation (will be implemented in runtime.c)
typedef enum {
    TAG_INT,
    TAG_BOOL,
    TAG_STRING,
    TAG_QUOTATION,
    TAG_VARIANT,
} ValueTag;

typedef struct StackCell {
    ValueTag tag;
    union {
        int64_t i;
        bool b;
        char* s;
        void* quotation;
        struct Variant* variant;
    } value;
    struct StackCell* next;
} StackCell;
```

## Compilation Strategy

1. **Words → LLVM Functions**
   - Each word becomes an LLVM function taking stack pointer
   - Returns new stack pointer after execution
   - Signature: `StackCell* word_name(StackCell* stack)`

2. **Expressions → Stack Operations**
   - Literals: push onto stack
   - Word calls: call corresponding function
   - Quotations: create closure, push pointer
   - Pattern matching: runtime type dispatch

3. **Built-in Operations**
   - Stack ops (dup, drop, swap, etc.) → runtime calls
   - Arithmetic (+, -, *, /) → pop operands, compute, push result
   - Comparisons (<, >, =) → pop, compare, push bool

## Example Compilation

```cem
: square ( Int -- Int )
  dup * ;
```

Compiles to:

```llvm
define %StackCell* @square(%StackCell* %stack) {
entry:
  %stack1 = call %StackCell* @dup(%StackCell* %stack)
  %stack2 = call %StackCell* @multiply(%StackCell* %stack1)
  ret %StackCell* %stack2
}
```

## Phases

Phase 1: Basic infrastructure (this file)
Phase 2: Primitive operations (primitives.rs)
Phase 3: Word compilation (words.rs)
Phase 4: Pattern matching (patterns.rs)
Phase 5: Optimization passes (optimize.rs)
*/

pub mod error;
pub mod primitives;
pub mod runtime;

pub use error::{CodegenError, CodegenResult};

use crate::ast::{Expr, Program, WordDef};
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::types::PointerType;
use inkwell::values::{FunctionValue, PointerValue};
use inkwell::AddressSpace;
use std::collections::HashMap;

/// Main code generator for Cem programs
pub struct CodeGen<'ctx> {
    pub context: &'ctx Context,
    pub module: Module<'ctx>,
    pub builder: Builder<'ctx>,

    /// Type of stack cell (opaque struct pointer)
    stack_cell_type: PointerType<'ctx>,

    /// Map from word names to compiled functions
    functions: HashMap<String, FunctionValue<'ctx>>,
}

impl<'ctx> CodeGen<'ctx> {
    /// Create a new code generator
    pub fn new(context: &'ctx Context, module_name: &str) -> Self {
        let module = context.create_module(module_name);
        let builder = context.create_builder();

        // Define opaque stack cell type
        // In LLVM, this is just a pointer to an opaque struct
        // The actual definition will be in the C runtime
        let stack_cell_type = context.ptr_type(AddressSpace::default());

        CodeGen {
            context,
            module,
            builder,
            stack_cell_type,
            functions: HashMap::new(),
        }
    }

    /// Get the stack cell pointer type
    pub fn stack_type(&self) -> PointerType<'ctx> {
        self.stack_cell_type
    }

    /// Compile a complete program
    pub fn compile_program(&mut self, program: &Program) -> CodegenResult<()> {
        // Phase 0: Declare runtime functions
        self.declare_runtime_functions()?;

        // Phase 1: Declare all words (forward declarations)
        for word in &program.word_defs {
            self.declare_word(word)?;
        }

        // Phase 2: Compile all word bodies
        for word in &program.word_defs {
            self.compile_word(word)?;
        }

        // Phase 3: Verify the module
        if let Err(err) = self.module.verify() {
            return Err(CodegenError::VerificationError {
                message: err.to_string(),
            });
        }

        Ok(())
    }

    /// Declare a word (create function signature)
    fn declare_word(&mut self, word: &WordDef) -> CodegenResult<()> {
        // All words have signature: StackCell* -> StackCell*
        let fn_type = self.stack_type().fn_type(&[self.stack_type().into()], false);

        let function = self.module.add_function(&word.name, fn_type, None);

        // Set parameter name for clarity
        function.get_nth_param(0).unwrap().set_name("stack");

        self.functions.insert(word.name.clone(), function);

        Ok(())
    }

    /// Compile a word definition
    fn compile_word(&mut self, word: &WordDef) -> CodegenResult<()> {
        let function = self.functions.get(&word.name).ok_or_else(|| {
            CodegenError::UnknownWord {
                name: word.name.clone(),
                location: None,
            }
        })?;

        // Create entry block
        let entry_block = self.context.append_basic_block(*function, "entry");
        self.builder.position_at_end(entry_block);

        // Get initial stack pointer from parameter
        let mut stack = function.get_nth_param(0).unwrap().into_pointer_value();

        // Compile each expression in the body
        for expr in &word.body {
            stack = self.compile_expr(expr, stack)?;
        }

        // Return final stack pointer
        self.builder.build_return(Some(&stack)).map_err(|e| {
            CodegenError::LlvmError {
                operation: "build_return".to_string(),
                details: e.to_string(),
            }
        })?;

        Ok(())
    }

    /// Compile a single expression
    fn compile_expr(
        &mut self,
        expr: &Expr,
        stack: PointerValue<'ctx>,
    ) -> CodegenResult<PointerValue<'ctx>> {
        match expr {
            Expr::IntLit(n) => {
                // Push integer literal onto stack
                self.compile_push_int(*n, stack)
            }

            Expr::BoolLit(b) => {
                // Push boolean literal onto stack
                self.compile_push_bool(*b, stack)
            }

            Expr::StringLit(s) => {
                // Push string literal onto stack
                self.compile_push_string(s, stack)
            }

            Expr::WordCall(name) => {
                // Call word function
                self.compile_word_call(name, stack)
            }

            Expr::Quotation(_exprs) => Err(CodegenError::Unimplemented {
                feature: "quotations".to_string(),
            }),

            Expr::Match { branches: _ } => Err(CodegenError::Unimplemented {
                feature: "pattern matching".to_string(),
            }),

            Expr::If {
                then_branch: _,
                else_branch: _,
            } => Err(CodegenError::Unimplemented {
                feature: "if expressions".to_string(),
            }),

            Expr::While {
                condition: _,
                body: _,
            } => Err(CodegenError::Unimplemented {
                feature: "while loops".to_string(),
            }),
        }
    }

    /// Compile a word call
    fn compile_word_call(
        &mut self,
        name: &str,
        stack: PointerValue<'ctx>,
    ) -> CodegenResult<PointerValue<'ctx>> {
        // Check if it's a built-in primitive
        if let Some(new_stack) = self.compile_builtin(name, stack)? {
            return Ok(new_stack);
        }

        // Otherwise, call user-defined word
        let function = self.functions.get(name).ok_or_else(|| {
            CodegenError::UnknownWord {
                name: name.to_string(),
                location: None,
            }
        })?;

        let result = self
            .builder
            .build_call(*function, &[stack.into()], "call")
            .map_err(|e| CodegenError::LlvmError {
                operation: "build_call".to_string(),
                details: e.to_string(),
            })?;

        Ok(result
            .try_as_basic_value()
            .left()
            .unwrap()
            .into_pointer_value())
    }

    // compile_builtin is implemented in primitives.rs

    /// Helper: Call a runtime function with arguments
    fn call_runtime<'a>(
        &mut self,
        fn_name: &str,
        args: &[inkwell::values::BasicMetadataValueEnum<'ctx>],
    ) -> CodegenResult<PointerValue<'ctx>> {
        let function = self.get_runtime_function(fn_name)?;

        let result = self
            .builder
            .build_call(function, args, fn_name)
            .map_err(|e| CodegenError::LlvmError {
                operation: fn_name.to_string(),
                details: e.to_string(),
            })?;

        Ok(result
            .try_as_basic_value()
            .left()
            .unwrap()
            .into_pointer_value())
    }

    /// Push an integer literal onto the stack
    fn compile_push_int(
        &mut self,
        value: i64,
        stack: PointerValue<'ctx>,
    ) -> CodegenResult<PointerValue<'ctx>> {
        let int_val = self.context.i64_type().const_int(value as u64, true);
        self.call_runtime("push_int", &[stack.into(), int_val.into()])
    }

    /// Push a boolean literal onto the stack
    fn compile_push_bool(
        &mut self,
        value: bool,
        stack: PointerValue<'ctx>,
    ) -> CodegenResult<PointerValue<'ctx>> {
        let bool_val = self.context.bool_type().const_int(value as u64, false);
        self.call_runtime("push_bool", &[stack.into(), bool_val.into()])
    }

    /// Push a string literal onto the stack
    fn compile_push_string(
        &mut self,
        value: &str,
        stack: PointerValue<'ctx>,
    ) -> CodegenResult<PointerValue<'ctx>> {
        // Create global string constant
        let string_global = self
            .builder
            .build_global_string_ptr(value, "str")
            .map_err(|e| CodegenError::LlvmError {
                operation: "build_global_string_ptr".to_string(),
                details: e.to_string(),
            })?;

        self.call_runtime(
            "push_string",
            &[stack.into(), string_global.as_pointer_value().into()],
        )
    }

    /// Generate LLVM IR as a string
    pub fn emit_ir(&self) -> String {
        self.module.print_to_string().to_string()
    }

    /// Write LLVM IR to a file
    pub fn emit_to_file(&self, path: &str) -> CodegenResult<()> {
        self.module
            .print_to_file(path)
            .map_err(|e| CodegenError::LlvmError {
                operation: "write_to_file".to_string(),
                details: e.to_string(),
            })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ast::{Expr, WordDef};
    use crate::ast::types::{Effect, StackType, Type};

    #[test]
    fn test_codegen_basic() {
        let context = Context::create();
        let mut codegen = CodeGen::new(&context, "test_module");

        // Simple word: : five ( -- Int ) 5 ;
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

        codegen.compile_program(&program).unwrap();

        let ir = codegen.emit_ir();

        // Check that the function was generated
        assert!(ir.contains("define"));
        assert!(ir.contains("@five"));
        assert!(ir.contains("push_int"));
    }

    #[test]
    fn test_codegen_word_call() {
        let context = Context::create();
        let mut codegen = CodeGen::new(&context, "test_module");

        // : double ( Int -- Int ) dup + ;
        let word = WordDef {
            name: "double".to_string(),
            effect: Effect {
                inputs: StackType::Empty.push(Type::Int),
                outputs: StackType::Empty.push(Type::Int),
            },
            body: vec![
                Expr::WordCall("dup".to_string()),
                Expr::WordCall("+".to_string()),
            ],
        };

        let program = Program {
            type_defs: vec![],
            word_defs: vec![word],
        };

        codegen.compile_program(&program).unwrap();

        let ir = codegen.emit_ir();
        assert!(ir.contains("@double"));
    }

    #[test]
    fn test_codegen_boolean() {
        let context = Context::create();
        let mut codegen = CodeGen::new(&context, "test_module");

        // : truth ( -- Bool ) true ;
        let word = WordDef {
            name: "truth".to_string(),
            effect: Effect {
                inputs: StackType::Empty,
                outputs: StackType::Empty.push(Type::Bool),
            },
            body: vec![Expr::BoolLit(true)],
        };

        let program = Program {
            type_defs: vec![],
            word_defs: vec![word],
        };

        codegen.compile_program(&program).unwrap();

        let ir = codegen.emit_ir();
        assert!(ir.contains("@truth"));
        assert!(ir.contains("push_bool"));
    }

    #[test]
    fn test_codegen_string() {
        let context = Context::create();
        let mut codegen = CodeGen::new(&context, "test_module");

        // : hello ( -- String ) "world" ;
        let word = WordDef {
            name: "hello".to_string(),
            effect: Effect {
                inputs: StackType::Empty,
                outputs: StackType::Empty.push(Type::String),
            },
            body: vec![Expr::StringLit("world".to_string())],
        };

        let program = Program {
            type_defs: vec![],
            word_defs: vec![word],
        };

        codegen.compile_program(&program).unwrap();

        let ir = codegen.emit_ir();
        assert!(ir.contains("@hello"));
        assert!(ir.contains("push_string"));
        assert!(ir.contains("world"));
    }

    #[test]
    fn test_codegen_multiple_words() {
        let context = Context::create();
        let mut codegen = CodeGen::new(&context, "test_module");

        // : five ( -- Int ) 5 ;
        // : ten ( -- Int ) 10 ;
        // : add-them ( -- Int ) five ten + ;
        let program = Program {
            type_defs: vec![],
            word_defs: vec![
                WordDef {
                    name: "five".to_string(),
                    effect: Effect {
                        inputs: StackType::Empty,
                        outputs: StackType::Empty.push(Type::Int),
                    },
                    body: vec![Expr::IntLit(5)],
                },
                WordDef {
                    name: "ten".to_string(),
                    effect: Effect {
                        inputs: StackType::Empty,
                        outputs: StackType::Empty.push(Type::Int),
                    },
                    body: vec![Expr::IntLit(10)],
                },
                WordDef {
                    name: "add_them".to_string(),
                    effect: Effect {
                        inputs: StackType::Empty,
                        outputs: StackType::Empty.push(Type::Int),
                    },
                    body: vec![
                        Expr::WordCall("five".to_string()),
                        Expr::WordCall("ten".to_string()),
                        Expr::WordCall("+".to_string()),
                    ],
                },
            ],
        };

        codegen.compile_program(&program).unwrap();

        let ir = codegen.emit_ir();
        assert!(ir.contains("@five"));
        assert!(ir.contains("@ten"));
        assert!(ir.contains("@add_them"));
    }

    #[test]
    fn test_codegen_unknown_word_error() {
        let context = Context::create();
        let mut codegen = CodeGen::new(&context, "test_module");

        // : bad ( -- Int ) unknown_word ;
        let word = WordDef {
            name: "bad".to_string(),
            effect: Effect {
                inputs: StackType::Empty,
                outputs: StackType::Empty.push(Type::Int),
            },
            body: vec![Expr::WordCall("unknown_word".to_string())],
        };

        let program = Program {
            type_defs: vec![],
            word_defs: vec![word],
        };

        let result = codegen.compile_program(&program);
        assert!(result.is_err());

        if let Err(CodegenError::UnknownWord { name, .. }) = result {
            assert_eq!(name, "unknown_word");
        } else {
            panic!("Expected UnknownWord error");
        }
    }

    #[test]
    fn test_codegen_unimplemented_features() {
        let context = Context::create();
        let mut codegen = CodeGen::new(&context, "test_module");

        // Test quotations
        let word = WordDef {
            name: "test_quot".to_string(),
            effect: Effect {
                inputs: StackType::Empty,
                outputs: StackType::Empty,
            },
            body: vec![Expr::Quotation(vec![Expr::IntLit(42)])],
        };

        let program = Program {
            type_defs: vec![],
            word_defs: vec![word],
        };

        let result = codegen.compile_program(&program);
        assert!(result.is_err());

        if let Err(CodegenError::Unimplemented { feature }) = result {
            assert_eq!(feature, "quotations");
        } else {
            panic!("Expected Unimplemented error");
        }
    }

    #[test]
    fn test_emit_to_file() {
        use std::fs;

        let context = Context::create();
        let mut codegen = CodeGen::new(&context, "test_module");

        let word = WordDef {
            name: "test".to_string(),
            effect: Effect {
                inputs: StackType::Empty,
                outputs: StackType::Empty.push(Type::Int),
            },
            body: vec![Expr::IntLit(42)],
        };

        let program = Program {
            type_defs: vec![],
            word_defs: vec![word],
        };

        codegen.compile_program(&program).unwrap();

        let temp_file = "/tmp/test_cem_output.ll";
        codegen.emit_to_file(temp_file).unwrap();

        assert!(fs::metadata(temp_file).is_ok());
        let contents = fs::read_to_string(temp_file).unwrap();
        assert!(contents.contains("@test"));

        // Cleanup
        fs::remove_file(temp_file).ok();
    }
}
