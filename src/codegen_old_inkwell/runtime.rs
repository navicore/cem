/**
Runtime function declarations and verification

This module declares all runtime functions that the generated code depends on.
It ensures that all required runtime functions are properly declared with correct
signatures before code generation begins.
*/

use super::{CodeGen, CodegenError, CodegenResult};

/// Runtime function signature information
pub struct RuntimeFunction {
    pub name: &'static str,
    pub description: &'static str,
}

/// All runtime functions that must be provided
pub const RUNTIME_FUNCTIONS: &[RuntimeFunction] = &[
    // Stack operations
    RuntimeFunction {
        name: "dup",
        description: "Duplicate top stack element",
    },
    RuntimeFunction {
        name: "drop",
        description: "Remove top stack element",
    },
    RuntimeFunction {
        name: "swap",
        description: "Swap top two stack elements",
    },
    RuntimeFunction {
        name: "over",
        description: "Copy second element to top",
    },
    RuntimeFunction {
        name: "rot",
        description: "Rotate top three elements",
    },
    // Arithmetic operations
    RuntimeFunction {
        name: "add",
        description: "Add top two integers",
    },
    RuntimeFunction {
        name: "subtract",
        description: "Subtract top two integers",
    },
    RuntimeFunction {
        name: "multiply",
        description: "Multiply top two integers",
    },
    RuntimeFunction {
        name: "divide",
        description: "Divide top two integers",
    },
    // Comparison operations
    RuntimeFunction {
        name: "less_than",
        description: "Compare if first < second",
    },
    RuntimeFunction {
        name: "greater_than",
        description: "Compare if first > second",
    },
    RuntimeFunction {
        name: "equal",
        description: "Compare if first == second",
    },
    // Push operations
    RuntimeFunction {
        name: "push_int",
        description: "Push integer onto stack",
    },
    RuntimeFunction {
        name: "push_bool",
        description: "Push boolean onto stack",
    },
    RuntimeFunction {
        name: "push_string",
        description: "Push string onto stack",
    },
    // Control flow
    RuntimeFunction {
        name: "call_quotation",
        description: "Call a quotation",
    },
    RuntimeFunction {
        name: "if_then_else",
        description: "Conditional execution",
    },
];

impl<'ctx> CodeGen<'ctx> {
    /// Declare all required runtime functions
    ///
    /// This should be called once at the beginning of compilation to ensure
    /// all runtime functions are properly declared.
    pub fn declare_runtime_functions(&mut self) -> CodegenResult<()> {
        // Stack operations (StackCell* -> StackCell*)
        for func in &[
            "dup", "drop", "swap", "over", "rot", "add", "subtract", "multiply", "divide",
            "less_than", "greater_than", "equal", "call_quotation", "if_then_else",
        ] {
            let fn_type = self.stack_type().fn_type(&[self.stack_type().into()], false);
            self.module.add_function(func, fn_type, None);
        }

        // push_int (StackCell*, i64 -> StackCell*)
        let push_int_type = self.stack_type().fn_type(
            &[self.stack_type().into(), self.context.i64_type().into()],
            false,
        );
        self.module.add_function("push_int", push_int_type, None);

        // push_bool (StackCell*, bool -> StackCell*)
        let push_bool_type = self.stack_type().fn_type(
            &[self.stack_type().into(), self.context.bool_type().into()],
            false,
        );
        self.module.add_function("push_bool", push_bool_type, None);

        // push_string (StackCell*, ptr -> StackCell*)
        let push_string_type = self.stack_type().fn_type(
            &[
                self.stack_type().into(),
                self.context.ptr_type(inkwell::AddressSpace::default()).into(),
            ],
            false,
        );
        self.module.add_function("push_string", push_string_type, None);

        Ok(())
    }

    /// Verify that a runtime function is properly declared
    pub fn verify_runtime_function(&self, name: &str) -> CodegenResult<()> {
        if self.module.get_function(name).is_none() {
            return Err(CodegenError::RuntimeError {
                function: name.to_string(),
                reason: "Runtime function not declared. Call declare_runtime_functions() first."
                    .to_string(),
            });
        }
        Ok(())
    }

    /// Get a declared runtime function, or return an error if not found
    pub fn get_runtime_function(
        &self,
        name: &str,
    ) -> CodegenResult<inkwell::values::FunctionValue<'ctx>> {
        self.module.get_function(name).ok_or_else(|| {
            CodegenError::RuntimeError {
                function: name.to_string(),
                reason: "Runtime function not found. Call declare_runtime_functions() first."
                    .to_string(),
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use inkwell::context::Context;

    #[test]
    fn test_declare_runtime_functions() {
        let context = Context::create();
        let mut codegen = CodeGen::new(&context, "test");

        codegen.declare_runtime_functions().unwrap();

        // Verify all runtime functions are declared
        for func in RUNTIME_FUNCTIONS {
            assert!(codegen.module.get_function(func.name).is_some());
        }
    }

    #[test]
    fn test_verify_runtime_function() {
        let context = Context::create();
        let mut codegen = CodeGen::new(&context, "test");

        // Should fail before declaration
        assert!(codegen.verify_runtime_function("dup").is_err());

        // Declare functions
        codegen.declare_runtime_functions().unwrap();

        // Should succeed after declaration
        assert!(codegen.verify_runtime_function("dup").is_ok());
    }

    #[test]
    fn test_get_runtime_function() {
        let context = Context::create();
        let mut codegen = CodeGen::new(&context, "test");

        codegen.declare_runtime_functions().unwrap();

        let dup_fn = codegen.get_runtime_function("dup").unwrap();
        assert_eq!(dup_fn.get_name().to_str().unwrap(), "dup");
    }
}
