/// Cem - A concatenative language with linear types
/// Pronounced "seam"
///
/// This crate implements the Cem compiler, including:
/// - Abstract syntax tree (AST) representation
/// - Type checker with effect inference
/// - Pattern matching exhaustiveness checking
/// - LLVM code generation (future)

pub mod ast;
pub mod typechecker;
pub mod parser;

pub use ast::{Program, WordDef, TypeDef, Expr};
pub use ast::types::{Type, Effect, StackType};
