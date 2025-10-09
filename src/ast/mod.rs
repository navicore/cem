/// Abstract Syntax Tree definitions for Cem
///
/// This module defines the core AST types representing Cem programs.

pub mod types;

use std::fmt;

/// A complete Cem program
#[derive(Debug, Clone, PartialEq)]
pub struct Program {
    pub type_defs: Vec<TypeDef>,
    pub word_defs: Vec<WordDef>,
}

/// Type definition (Algebraic Data Type / Sum Type)
#[derive(Debug, Clone, PartialEq)]
pub struct TypeDef {
    pub name: String,
    pub type_params: Vec<String>,
    pub variants: Vec<Variant>,
}

/// A variant of a sum type
#[derive(Debug, Clone, PartialEq)]
pub struct Variant {
    pub name: String,
    pub fields: Vec<types::Type>,
}

/// Word (function) definition
#[derive(Debug, Clone, PartialEq)]
pub struct WordDef {
    pub name: String,
    pub effect: types::Effect,
    pub body: Vec<Expr>,
}

/// Expression in the body of a word
#[derive(Debug, Clone, PartialEq)]
pub enum Expr {
    /// Literal integer
    IntLit(i64),

    /// Literal boolean
    BoolLit(bool),

    /// Literal string
    StringLit(String),

    /// Word call (reference to another word)
    WordCall(String),

    /// Quotation (code block)
    Quotation(Vec<Expr>),

    /// Pattern match expression
    Match {
        branches: Vec<MatchBranch>,
    },

    /// If expression (condition is top of stack)
    If {
        then_branch: Box<Expr>,
        else_branch: Box<Expr>,
    },

    /// While loop
    While {
        condition: Box<Expr>,
        body: Box<Expr>,
    },
}

/// A branch in a pattern match
#[derive(Debug, Clone, PartialEq)]
pub struct MatchBranch {
    pub pattern: Pattern,
    pub body: Vec<Expr>,
}

/// Pattern for matching on sum types
#[derive(Debug, Clone, PartialEq)]
pub enum Pattern {
    /// Match a specific variant, binding its fields
    Variant {
        name: String,
        // Field patterns could be added later for nested matching
    },
}

impl fmt::Display for Expr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Expr::IntLit(n) => write!(f, "{}", n),
            Expr::BoolLit(b) => write!(f, "{}", b),
            Expr::StringLit(s) => write!(f, "\"{}\"", s),
            Expr::WordCall(name) => write!(f, "{}", name),
            Expr::Quotation(exprs) => {
                write!(f, "[ ")?;
                for expr in exprs {
                    write!(f, "{} ", expr)?;
                }
                write!(f, "]")
            }
            Expr::Match { branches } => {
                writeln!(f, "match")?;
                for branch in branches {
                    writeln!(f, "  {:?} => [ ... ]", branch.pattern)?;
                }
                write!(f, "end")
            }
            Expr::If { .. } => write!(f, "if"),
            Expr::While { .. } => write!(f, "while"),
        }
    }
}
