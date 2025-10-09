/// Type checker for Cem
///
/// This module implements bidirectional type checking with:
/// - Stack effect inference
/// - Row polymorphism
/// - Linear type tracking
/// - Pattern matching exhaustiveness

pub mod environment;
pub mod checker;
pub mod unification;
pub mod errors;

pub use checker::TypeChecker;
pub use errors::{TypeError, TypeResult};
