/**
Core type checker for Cem

Implements bidirectional type checking with stack effect inference.
*/
use crate::ast::types::{Effect, StackType, Type};
use crate::ast::{Expr, MatchBranch, Pattern, Program, WordDef};
use crate::typechecker::environment::Environment;
use crate::typechecker::errors::{TypeError, TypeResult};
use crate::typechecker::unification::{unify_stack_types, unify_types};

/// The main type checker
pub struct TypeChecker {
    env: Environment,
}

impl TypeChecker {
    /// Create a new type checker
    pub fn new() -> Self {
        TypeChecker {
            env: Environment::new(),
        }
    }

    /// Type check a complete program
    pub fn check_program(&mut self, program: &Program) -> TypeResult<()> {
        // First pass: add all type definitions
        for typedef in &program.type_defs {
            self.env.add_type(typedef.clone());
        }

        // Second pass: check all word definitions
        for word_def in &program.word_defs {
            self.check_word_def(word_def)?;
        }

        Ok(())
    }

    /// Type check a word definition
    fn check_word_def(&mut self, word: &WordDef) -> TypeResult<()> {
        // Start with the input stack from the declared effect
        let mut current_stack = word.effect.inputs.clone();

        // Type check each expression in the body
        for expr in &word.body {
            current_stack = self.check_expr(expr, current_stack)?;
        }

        // Verify final stack matches declared output effect
        let (_, _) = unify_stack_types(&current_stack, &word.effect.outputs).map_err(|_| {
            TypeError::EffectMismatch {
                expected: word.effect.clone(),
                actual: Effect::new(word.effect.inputs.clone(), current_stack),
                word: word.name.clone(),
            }
        })?;

        // Add word to environment for future lookups
        self.env.add_word(word.name.clone(), word.effect.clone());

        Ok(())
    }

    /// Type check an expression, returning the resulting stack type
    fn check_expr(&self, expr: &Expr, stack: StackType) -> TypeResult<StackType> {
        match expr {
            Expr::IntLit(_) => {
                // Push Int onto stack
                Ok(stack.push(Type::Int))
            }

            Expr::BoolLit(_) => {
                // Push Bool onto stack
                Ok(stack.push(Type::Bool))
            }

            Expr::StringLit(_) => {
                // Push String onto stack
                Ok(stack.push(Type::String))
            }

            Expr::WordCall(name) => {
                // Look up word effect
                let effect = self
                    .env
                    .lookup_word(name)
                    .ok_or_else(|| TypeError::UndefinedWord { name: name.clone() })?;

                // Apply effect to current stack
                self.apply_effect(effect, stack, name)
            }

            Expr::Quotation(_exprs) => {
                // For now, treat quotations as opaque
                // In future: infer the quotation's effect
                // For now: push a generic quotation type
                let quotation_effect = Effect::new(StackType::empty(), StackType::empty());
                Ok(stack.push(Type::Quotation(Box::new(quotation_effect))))
            }

            Expr::Match { branches } => {
                // Pattern matching
                self.check_match(branches, stack)
            }

            Expr::If {
                then_branch,
                else_branch,
            } => {
                // Pop Bool from stack
                let (stack_after_cond, cond_type) =
                    stack.pop().ok_or_else(|| TypeError::StackUnderflow {
                        word: "if".to_string(),
                        required: 1,
                        available: 0,
                    })?;

                // Verify condition is Bool
                unify_types(&cond_type, &Type::Bool).map_err(|_| TypeError::TypeMismatch {
                    expected: Type::Bool,
                    actual: cond_type,
                    context: "if condition".to_string(),
                })?;

                // Check both branches produce same stack
                let then_stack = self.check_expr(then_branch, stack_after_cond.clone())?;
                let else_stack = self.check_expr(else_branch, stack_after_cond)?;

                // Unify branch results
                let (_, _) =
                    unify_stack_types(&then_stack, &else_stack).map_err(|_| TypeError::Other {
                        message: "if branches produce incompatible stack effects".to_string(),
                    })?;

                Ok(then_stack)
            }

            Expr::While { condition, body } => {
                // While loop: condition and body must maintain stack shape
                // This is a simplified check - full check would verify convergence
                // For now, just verify condition produces Bool and body maintains stack
                let cond_stack = self.check_expr(condition, stack.clone())?;

                // Pop Bool from condition result
                let (_stack_after_cond, cond_type) =
                    cond_stack.pop().ok_or_else(|| TypeError::StackUnderflow {
                        word: "while".to_string(),
                        required: 1,
                        available: 0,
                    })?;

                unify_types(&cond_type, &Type::Bool).map_err(|_| TypeError::TypeMismatch {
                    expected: Type::Bool,
                    actual: cond_type,
                    context: "while condition".to_string(),
                })?;

                // Check body maintains stack shape
                let body_stack = self.check_expr(body, stack.clone())?;
                let (_, _) =
                    unify_stack_types(&stack, &body_stack).map_err(|_| TypeError::Other {
                        message: "while body must maintain stack shape".to_string(),
                    })?;

                Ok(stack)
            }
        }
    }

    /// Apply a word's effect to the current stack
    fn apply_effect(
        &self,
        effect: &Effect,
        stack: StackType,
        word_name: &str,
    ) -> TypeResult<StackType> {
        // Try to unify the effect's input with the current stack
        // This handles polymorphic effects like dup: (A -- A A)

        let input_depth = effect.inputs.depth().unwrap_or(0);
        let stack_depth = stack.depth().unwrap_or(0);

        if stack_depth < input_depth {
            return Err(TypeError::StackUnderflow {
                word: word_name.to_string(),
                required: input_depth,
                available: stack_depth,
            });
        }

        // For simple case: try unification
        // Split the stack into "will be consumed" and "will remain"
        let mut remaining_stack = stack.clone();
        let mut consumed = Vec::new();

        // Pop the elements that will be consumed
        for _ in 0..input_depth {
            if let Some((rest, top)) = remaining_stack.pop() {
                consumed.push(top);
                remaining_stack = rest;
            } else {
                return Err(TypeError::StackUnderflow {
                    word: word_name.to_string(),
                    required: input_depth,
                    available: consumed.len(),
                });
            }
        }

        // Reverse to get bottom-to-top order
        consumed.reverse();

        // Now unify consumed types with effect.inputs
        let consumed_stack = StackType::from_vec(consumed);
        let (type_subst, _stack_subst) = unify_stack_types(&consumed_stack, &effect.inputs)
            .map_err(|e| TypeError::Other {
                message: format!("Cannot apply '{}': input type mismatch: {}", word_name, e),
            })?;

        // Apply substitution to outputs
        let output_stack = self.apply_type_substitution(&effect.outputs, &type_subst);

        // Rebuild stack: remaining + outputs
        let mut result = remaining_stack;
        let mut outputs_vec = Vec::new();
        let mut temp = output_stack;
        while let Some((rest, top)) = temp.pop() {
            outputs_vec.push(top);
            temp = rest;
        }
        outputs_vec.reverse();
        for ty in outputs_vec {
            result = result.push(ty);
        }

        Ok(result)
    }

    /// Apply type substitution to a stack type
    fn apply_type_substitution(
        &self,
        stack: &StackType,
        subst: &crate::typechecker::unification::Substitution,
    ) -> StackType {
        match stack {
            StackType::Empty => StackType::Empty,
            StackType::Cons { rest, top } => {
                let new_rest = self.apply_type_substitution(rest, subst);
                let new_top = self.apply_type_subst_to_type(top, subst);
                new_rest.push(new_top)
            }
            StackType::RowVar(name) => {
                // Row variables don't get substituted here (would need stack substitution)
                StackType::RowVar(name.clone())
            }
        }
    }

    /// Apply type substitution to a type
    fn apply_type_subst_to_type(
        &self,
        ty: &Type,
        subst: &crate::typechecker::unification::Substitution,
    ) -> Type {
        match ty {
            Type::Var(name) => subst.get(name).cloned().unwrap_or_else(|| ty.clone()),
            Type::Named { name, args } => Type::Named {
                name: name.clone(),
                args: args
                    .iter()
                    .map(|arg| self.apply_type_subst_to_type(arg, subst))
                    .collect(),
            },
            Type::Quotation(eff) => {
                // Would need to substitute in effect too
                Type::Quotation(eff.clone())
            }
            _ => ty.clone(),
        }
    }

    /// Type check a pattern match
    fn check_match(&self, branches: &[MatchBranch], stack: StackType) -> TypeResult<StackType> {
        if branches.is_empty() {
            return Err(TypeError::Other {
                message: "Empty pattern match".to_string(),
            });
        }

        // Pop the scrutinee from stack
        let (stack_after_pop, scrutinee_type) =
            stack.pop().ok_or_else(|| TypeError::StackUnderflow {
                word: "match".to_string(),
                required: 1,
                available: 0,
            })?;

        // Get the type name from scrutinee
        let type_name = match &scrutinee_type {
            Type::Named { name, .. } => name.clone(),
            _ => {
                return Err(TypeError::Other {
                    message: format!("Cannot pattern match on non-ADT type: {}", scrutinee_type),
                })
            }
        };

        // Check exhaustiveness (all variants covered)
        let variants =
            self.env
                .get_variants(&type_name)
                .ok_or_else(|| TypeError::UndefinedType {
                    name: type_name.clone(),
                })?;

        let covered_variants: Vec<_> = branches
            .iter()
            .map(|b| match &b.pattern {
                Pattern::Variant { name } => name.as_str(),
            })
            .collect();

        let missing: Vec<_> = variants
            .iter()
            .filter(|v| !covered_variants.contains(&v.name.as_str()))
            .map(|v| v.name.clone())
            .collect();

        if !missing.is_empty() {
            return Err(TypeError::NonExhaustiveMatch {
                type_name: type_name.clone(),
                missing_variants: missing,
            });
        }

        // Type check each branch and verify they all produce same effect
        let mut branch_results = Vec::new();

        for branch in branches {
            // Get the variant definition
            let variant = variants
                .iter()
                .find(|v| match &branch.pattern {
                    Pattern::Variant { name } => v.name == *name,
                })
                .ok_or_else(|| TypeError::Other {
                    message: format!("Unknown variant in pattern"),
                })?;

            // Pattern destructures: push variant fields onto stack
            let mut branch_stack = stack_after_pop.clone();
            for field_type in &variant.fields {
                branch_stack = branch_stack.push(field_type.clone());
            }

            // Type check branch body
            for expr in &branch.body {
                branch_stack = self.check_expr(expr, branch_stack)?;
            }

            branch_results.push(branch_stack);
        }

        // All branches must produce the same stack effect
        let first_result = &branch_results[0];
        for (i, result) in branch_results.iter().enumerate().skip(1) {
            let (_, _) = unify_stack_types(first_result, result).map_err(|_| {
                TypeError::InconsistentBranchEffects {
                    type_name: type_name.clone(),
                    expected: Effect::new(stack_after_pop.clone(), first_result.clone()),
                    actual: Effect::new(stack_after_pop.clone(), result.clone()),
                    branch: format!("branch {}", i),
                }
            })?;
        }

        Ok(first_result.clone())
    }
}

impl Default for TypeChecker {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    // Test imports (currently unused)

    #[test]
    fn test_check_literals() {
        let checker = TypeChecker::new();
        let stack = StackType::empty();

        // Int literal
        let result = checker.check_expr(&Expr::IntLit(42), stack.clone());
        assert!(result.is_ok());
        let stack_with_int = result.unwrap();
        assert_eq!(stack_with_int.depth(), Some(1));

        // Bool literal
        let result = checker.check_expr(&Expr::BoolLit(true), stack.clone());
        assert!(result.is_ok());
    }

    #[test]
    fn test_check_builtin_word() {
        let checker = TypeChecker::new();

        // Start with Int on stack
        let stack = StackType::empty().push(Type::Int);

        // Call dup
        let result = checker.check_expr(&Expr::WordCall("dup".to_string()), stack);
        if let Err(e) = &result {
            eprintln!("Error: {:?}", e);
        }
        assert!(result.is_ok());
        let result_stack = result.unwrap();
        assert_eq!(result_stack.depth(), Some(2));
    }

    #[test]
    fn test_undefined_word() {
        let checker = TypeChecker::new();
        let stack = StackType::empty();

        let result = checker.check_expr(&Expr::WordCall("unknown".to_string()), stack);
        assert!(result.is_err());
        match result.unwrap_err() {
            TypeError::UndefinedWord { name } => assert_eq!(name, "unknown"),
            _ => panic!("Expected UndefinedWord error"),
        }
    }

    #[test]
    fn test_stack_underflow() {
        let checker = TypeChecker::new();
        let stack = StackType::empty(); // Empty stack

        // Try to call + which needs 2 ints
        let result = checker.check_expr(&Expr::WordCall("+".to_string()), stack);
        assert!(result.is_err());
        match result.unwrap_err() {
            TypeError::StackUnderflow { .. } => (),
            e => panic!("Expected StackUnderflow, got {:?}", e),
        }
    }
}
