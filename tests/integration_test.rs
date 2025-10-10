/**
End-to-end integration test: Cem source → LLVM IR → executable
*/

use cem::ast::{Expr, Program, WordDef};
use cem::ast::types::{Effect, StackType, Type};
use cem::codegen::{CodeGen, compile_to_object};
use std::process::Command;

#[test]
fn test_end_to_end_compilation() {
    // Build the runtime first
    let runtime_status = Command::new("just")
        .arg("build-runtime")
        .status()
        .expect("Failed to build runtime");
    
    assert!(runtime_status.success(), "Runtime build failed");

    // Create a simple program: : fortytwo ( -- Int ) 42 ;
    let word = WordDef {
        name: "fortytwo".to_string(),
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

    // Generate LLVM IR
    let mut codegen = CodeGen::new();
    let ir = codegen.compile_program(&program).expect("Failed to generate IR");

    // Verify IR contains expected elements
    assert!(ir.contains("define ptr @fortytwo"));
    assert!(ir.contains("call ptr @push_int"));
    assert!(ir.contains("i64 42"));

    // Compile to object file (tests that LLVM accepts our IR)
    compile_to_object(&ir, "test_fortytwo")
        .expect("Failed to compile IR to object");

    // Clean up
    std::fs::remove_file("test_fortytwo.o").ok();
    std::fs::remove_file("test_fortytwo.ll").ok();
}

#[test]
fn test_arithmetic_compilation() {
    // Build runtime
    let runtime_status = Command::new("just")
        .arg("build-runtime")
        .status()
        .expect("Failed to build runtime");
    
    assert!(runtime_status.success());

    // : eight ( -- Int ) 5 3 + ;
    let word = WordDef {
        name: "eight".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::IntLit(5),
            Expr::IntLit(3),
            Expr::WordCall("add".to_string()),
        ],
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate and compile
    let mut codegen = CodeGen::new();
    let ir = codegen.compile_program(&program).expect("Failed to generate IR");

    assert!(ir.contains("@eight"));
    assert!(ir.contains("@add"));

    compile_to_object(&ir, "test_eight")
        .expect("Failed to compile");

    // Clean up
    std::fs::remove_file("test_eight.o").ok();
    std::fs::remove_file("test_eight.ll").ok();
}
