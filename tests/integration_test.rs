/**
End-to-end integration test: Cem source → LLVM IR → executable
*/

use cem::ast::{Expr, Program, WordDef};
use cem::ast::types::{Effect, StackType, Type};
use cem::codegen::{CodeGen, compile_to_object, link_program};
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

#[test]
fn test_executable_with_main() {
    // Build runtime
    let runtime_status = Command::new("just")
        .arg("build-runtime")
        .status()
        .expect("Failed to build runtime");

    assert!(runtime_status.success());

    // : fortytwo ( -- Int ) 42 ;
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

    // Generate IR with main() function
    let mut codegen = CodeGen::new();
    let ir = codegen.compile_program_with_main(&program, Some("fortytwo"))
        .expect("Failed to generate IR");

    // Verify IR contains main function
    assert!(ir.contains("define i32 @main()"));
    assert!(ir.contains("call ptr @fortytwo"));
    assert!(ir.contains("call void @print_stack"));
    assert!(ir.contains("ret i32 0"));

    // Link to produce executable
    link_program(&ir, "runtime/libcem_runtime.a", "test_fortytwo_exe")
        .expect("Failed to link");

    // Run the executable
    let output = Command::new("./test_fortytwo_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());

    // Check that it printed 42
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("42"), "Expected output to contain 42, got: {}", stdout);

    // Clean up
    std::fs::remove_file("test_fortytwo_exe").ok();
    std::fs::remove_file("test_fortytwo_exe.ll").ok();
}

#[test]
fn test_multiply_executable() {
    // Build runtime
    let runtime_status = Command::new("just")
        .arg("build-runtime")
        .status()
        .expect("Failed to build runtime");

    assert!(runtime_status.success());

    // : product ( -- Int ) 6 7 * ;
    let word = WordDef {
        name: "product".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::IntLit(6),
            Expr::IntLit(7),
            Expr::WordCall("multiply".to_string()),
        ],
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate and link
    let mut codegen = CodeGen::new();
    let ir = codegen.compile_program_with_main(&program, Some("product"))
        .expect("Failed to generate IR");

    link_program(&ir, "runtime/libcem_runtime.a", "test_product_exe")
        .expect("Failed to link");

    // Run and check output
    let output = Command::new("./test_product_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("42"), "Expected 42, got: {}", stdout);

    // Clean up
    std::fs::remove_file("test_product_exe").ok();
    std::fs::remove_file("test_product_exe.ll").ok();
}
