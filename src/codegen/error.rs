/**
Error types for code generation

This module defines proper error types for code generation failures,
replacing generic String errors with structured error types.
*/

use std::fmt;

/// Errors that can occur during code generation
#[derive(Debug, Clone, PartialEq)]
pub enum CodegenError {
    /// Unknown word referenced in code
    UnknownWord {
        name: String,
        location: Option<String>,
    },

    /// LLVM operation failed
    LlvmError {
        operation: String,
        details: String,
    },

    /// Module verification failed
    VerificationError {
        message: String,
    },

    /// Feature not yet implemented
    Unimplemented {
        feature: String,
    },

    /// Invalid program structure
    InvalidProgram {
        reason: String,
    },

    /// Runtime function not found or invalid
    RuntimeError {
        function: String,
        reason: String,
    },
}

impl fmt::Display for CodegenError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CodegenError::UnknownWord { name, location } => {
                write!(f, "Unknown word: {}", name)?;
                if let Some(loc) = location {
                    write!(f, " at {}", loc)?;
                }
                Ok(())
            }
            CodegenError::LlvmError { operation, details } => {
                write!(f, "LLVM error during {}: {}", operation, details)
            }
            CodegenError::VerificationError { message } => {
                write!(f, "Module verification failed: {}", message)
            }
            CodegenError::Unimplemented { feature } => {
                write!(f, "Feature not yet implemented: {}", feature)
            }
            CodegenError::InvalidProgram { reason } => {
                write!(f, "Invalid program: {}", reason)
            }
            CodegenError::RuntimeError { function, reason } => {
                write!(f, "Runtime function '{}': {}", function, reason)
            }
        }
    }
}

impl std::error::Error for CodegenError {}

/// Result type for code generation operations
pub type CodegenResult<T> = Result<T, CodegenError>;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_display() {
        let err = CodegenError::UnknownWord {
            name: "foo".to_string(),
            location: Some("line 42".to_string()),
        };
        assert_eq!(err.to_string(), "Unknown word: foo at line 42");

        let err = CodegenError::Unimplemented {
            feature: "pattern matching".to_string(),
        };
        assert_eq!(
            err.to_string(),
            "Feature not yet implemented: pattern matching"
        );
    }
}
