/**
Primitive Operations for Cem Runtime

This module handles compilation of built-in operations:
- Stack operations: dup, drop, swap, over, rot
- Arithmetic: +, -, *, /
- Comparisons: <, >, =, <=, >=, !=
- Boolean: and, or, not
- Control flow: if, call

Each primitive is implemented as a runtime function call.
*/

use super::{CodeGen, CodegenResult};
use inkwell::values::PointerValue;

impl<'ctx> CodeGen<'ctx> {
    /// Compile a built-in primitive operation
    pub fn compile_builtin(
        &mut self,
        name: &str,
        stack: PointerValue<'ctx>,
    ) -> CodegenResult<Option<PointerValue<'ctx>>> {
        match name {
            // Stack operations
            "dup" => self.compile_runtime_call("dup", stack),
            "drop" => self.compile_runtime_call("drop", stack),
            "swap" => self.compile_runtime_call("swap", stack),
            "over" => self.compile_runtime_call("over", stack),
            "rot" => self.compile_runtime_call("rot", stack),

            // Arithmetic operations
            "+" => self.compile_runtime_call("add", stack),
            "-" => self.compile_runtime_call("subtract", stack),
            "*" => self.compile_runtime_call("multiply", stack),
            "/" => self.compile_runtime_call("divide", stack),

            // Comparison operations
            "<" => self.compile_runtime_call("less_than", stack),
            ">" => self.compile_runtime_call("greater_than", stack),
            "=" => self.compile_runtime_call("equal", stack),

            // Control flow
            "call" => self.compile_runtime_call("call_quotation", stack),
            "if" => self.compile_runtime_call("if_then_else", stack),

            // Not a built-in
            _ => Ok(None),
        }
    }

    /// Compile a call to a runtime function
    fn compile_runtime_call(
        &mut self,
        fn_name: &str,
        stack: PointerValue<'ctx>,
    ) -> CodegenResult<Option<PointerValue<'ctx>>> {
        // Use the call_runtime helper from the parent module
        let result = self.call_runtime(fn_name, &[stack.into()])?;
        Ok(Some(result))
    }
}

/// List of all built-in primitive operations
pub const PRIMITIVES: &[&str] = &[
    // Stack operations
    "dup", "drop", "swap", "over", "rot",
    // Arithmetic
    "+", "-", "*", "/",
    // Comparisons
    "<", ">", "=",
    // Control flow
    "call", "if",
];

/// Check if a word name is a built-in primitive
pub fn is_primitive(name: &str) -> bool {
    PRIMITIVES.contains(&name)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_is_primitive() {
        assert!(is_primitive("dup"));
        assert!(is_primitive("+"));
        assert!(is_primitive("<"));
        assert!(!is_primitive("custom_word"));
    }
}
