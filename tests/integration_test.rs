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

#[test]
fn test_if_expression() {
    // Build runtime
    let runtime_status = Command::new("just")
        .arg("build-runtime")
        .status()
        .expect("Failed to build runtime");

    assert!(runtime_status.success());

    // : abs ( Int -- Int ) dup 0 < if [ 0 swap - ] [ ] ;
    // Simplified: : test_if ( -- Int ) true if [ 42 ] [ 0 ] ;
    let word = WordDef {
        name: "test_if".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::BoolLit(true),
            Expr::If {
                then_branch: Box::new(Expr::Quotation(vec![Expr::IntLit(42)])),
                else_branch: Box::new(Expr::Quotation(vec![Expr::IntLit(0)])),
            },
        ],
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate and link
    let mut codegen = CodeGen::new();
    let ir = codegen.compile_program_with_main(&program, Some("test_if"))
        .expect("Failed to generate IR");

    // Verify IR contains if/then/else structure
    assert!(ir.contains("br i1 %"));  // Branch on boolean condition
    assert!(ir.contains("then_"));
    assert!(ir.contains("else_"));
    assert!(ir.contains("merge_"));
    assert!(ir.contains("phi ptr"));

    link_program(&ir, "runtime/libcem_runtime.a", "test_if_exe")
        .expect("Failed to link");

    // Run and check output - should print 42 (true branch)
    let output = Command::new("./test_if_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("42"), "Expected 42 from true branch, got: {}", stdout);

    // Clean up
    std::fs::remove_file("test_if_exe").ok();
    std::fs::remove_file("test_if_exe.ll").ok();
}

#[test]
fn test_tail_call_optimization() {
    // Build runtime
    let runtime_status = Command::new("just")
        .arg("build-runtime")
        .status()
        .expect("Failed to build runtime");

    assert!(runtime_status.success());

    // Create a simple tail-recursive word that calls itself
    // : identity ( Int -- Int ) ;  (just returns input)
    let identity = WordDef {
        name: "identity".to_string(),
        effect: Effect {
            inputs: StackType::Empty.push(Type::Int),
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![],  // Identity - does nothing, returns stack as-is
    };

    // : call_identity ( -- Int ) 42 identity ;
    let call_identity = WordDef {
        name: "call_identity".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::IntLit(42),
            Expr::WordCall("identity".to_string()),
        ],
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![identity, call_identity],
    };

    // Generate IR
    let mut codegen = CodeGen::new();
    let ir = codegen.compile_program_with_main(&program, Some("call_identity"))
        .expect("Failed to generate IR");

    // Verify IR contains musttail for the last word call
    assert!(ir.contains("musttail call"), "Expected musttail optimization for tail call");

    // Link and run to verify it works
    link_program(&ir, "runtime/libcem_runtime.a", "test_tail_call_exe")
        .expect("Failed to link");

    let output = Command::new("./test_tail_call_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("42"), "Expected 42, got: {}", stdout);

    // Clean up
    std::fs::remove_file("test_tail_call_exe").ok();
    std::fs::remove_file("test_tail_call_exe.ll").ok();
}

#[test]
fn test_if_false_branch() {
    // Build runtime
    let runtime_status = Command::new("just")
        .arg("build-runtime")
        .status()
        .expect("Failed to build runtime");

    assert!(runtime_status.success());

    // : test_if_false ( -- Int ) false if [ 42 ] [ 99 ] ;
    // Should take the else branch and return 99
    let word = WordDef {
        name: "test_if_false".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::BoolLit(false),  // Push false
            Expr::If {
                then_branch: Box::new(Expr::Quotation(vec![Expr::IntLit(42)])),
                else_branch: Box::new(Expr::Quotation(vec![Expr::IntLit(99)])),
            },
        ],
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate and link
    let mut codegen = CodeGen::new();
    let ir = codegen.compile_program_with_main(&program, Some("test_if_false"))
        .expect("Failed to generate IR");

    link_program(&ir, "runtime/libcem_runtime.a", "test_if_false_exe")
        .expect("Failed to link");

    // Run and check output - should print 99 (false branch)
    let output = Command::new("./test_if_false_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("99"), "Expected 99 from false branch, got: {}", stdout);

    // Clean up
    std::fs::remove_file("test_if_false_exe").ok();
    std::fs::remove_file("test_if_false_exe.ll").ok();
}

#[test]
fn test_tail_call_in_if_branch() {
    // Build runtime
    let runtime_status = Command::new("just")
        .arg("build-runtime")
        .status()
        .expect("Failed to build runtime");

    assert!(runtime_status.success());

    // Create a helper word that just returns its input
    // : passthrough ( Int -- Int ) ;
    let passthrough = WordDef {
        name: "passthrough".to_string(),
        effect: Effect {
            inputs: StackType::Empty.push(Type::Int),
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![],  // Identity - returns stack as-is
    };

    // Create a word that calls another word in tail position within an if branch
    // : conditional_call ( Bool -- Int )
    //   if [ passthrough ] [ passthrough ] ;
    // This tests that tail calls inside if branches are optimized
    let conditional_call = WordDef {
        name: "conditional_call".to_string(),
        effect: Effect {
            inputs: StackType::Empty.push(Type::Bool),
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::If {
                // Both branches call passthrough in tail position
                // Then branch: push 42 then call passthrough
                then_branch: Box::new(Expr::Quotation(vec![
                    Expr::IntLit(42),
                    Expr::WordCall("passthrough".to_string()),
                ])),
                // Else branch: push 99 then call passthrough
                else_branch: Box::new(Expr::Quotation(vec![
                    Expr::IntLit(99),
                    Expr::WordCall("passthrough".to_string()),
                ])),
            },
        ],
    };

    // Entry word that sets up the test: push true, call conditional_call
    // : test_entry ( -- Int ) true conditional_call ;
    let test_entry = WordDef {
        name: "test_entry".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::BoolLit(true),
            Expr::WordCall("conditional_call".to_string()),
        ],
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![passthrough, conditional_call, test_entry],
    };

    // Generate IR
    let mut codegen = CodeGen::new();
    let ir = codegen.compile_program_with_main(&program, Some("test_entry"))
        .expect("Failed to generate IR");

    // Critical check: verify that passthrough calls in the if branches are tail-optimized
    // The IR should contain "musttail call ptr @passthrough" inside the branch blocks
    assert!(ir.contains("musttail call ptr @passthrough"),
        "Expected musttail optimization for tail calls in if branches");

    // Link and run to verify it works correctly
    link_program(&ir, "runtime/libcem_runtime.a", "test_tail_in_if_exe")
        .expect("Failed to link");

    let output = Command::new("./test_tail_in_if_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("42"), "Expected 42, got: {}", stdout);

    // Clean up
    std::fs::remove_file("test_tail_in_if_exe").ok();
    std::fs::remove_file("test_tail_in_if_exe.ll").ok();
}

#[test]
fn test_nested_if_expressions() {
    // Build runtime
    let runtime_status = Command::new("just")
        .arg("build-runtime")
        .status()
        .expect("Failed to build runtime");

    assert!(runtime_status.success());

    // Create a word with nested if expressions:
    // : nested_if ( Bool Bool -- Int )
    //   if
    //     [ if [ 1 ] [ 2 ] ]
    //     [ if [ 3 ] [ 4 ] ]
    //   ;
    // Tests: true true => 1, true false => 2, false true => 3, false false => 4
    let nested_if = WordDef {
        name: "nested_if".to_string(),
        effect: Effect {
            inputs: StackType::Empty.push(Type::Bool).push(Type::Bool),
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::If {
                // Outer if: first bool
                then_branch: Box::new(Expr::Quotation(vec![
                    // Inner if in then branch
                    Expr::If {
                        then_branch: Box::new(Expr::Quotation(vec![Expr::IntLit(1)])),
                        else_branch: Box::new(Expr::Quotation(vec![Expr::IntLit(2)])),
                    },
                ])),
                else_branch: Box::new(Expr::Quotation(vec![
                    // Inner if in else branch
                    Expr::If {
                        then_branch: Box::new(Expr::Quotation(vec![Expr::IntLit(3)])),
                        else_branch: Box::new(Expr::Quotation(vec![Expr::IntLit(4)])),
                    },
                ])),
            },
        ],
    };

    // Test case: true, true => should give 1
    let test_true_true = WordDef {
        name: "test_true_true".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::BoolLit(true),   // Inner condition
            Expr::BoolLit(true),   // Outer condition
            Expr::WordCall("nested_if".to_string()),
        ],
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![nested_if, test_true_true],
    };

    // Generate and link
    let mut codegen = CodeGen::new();
    let ir = codegen.compile_program_with_main(&program, Some("test_true_true"))
        .expect("Failed to generate IR");

    // Verify IR contains nested branching structure
    assert!(ir.contains("then_"), "Expected then branch labels");
    assert!(ir.contains("else_"), "Expected else branch labels");
    assert!(ir.contains("merge_"), "Expected merge block labels");

    // Save IR for debugging
    std::fs::write("test_nested_if_debug.ll", &ir).expect("Failed to write IR");

    link_program(&ir, "runtime/libcem_runtime.a", "test_nested_if_exe")
        .expect("Failed to link");

    // Run and check output - should print 1 (both true)
    let output = Command::new("./test_nested_if_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("1"), "Expected 1 from nested true/true case, got: {}", stdout);

    // Clean up
    std::fs::remove_file("test_nested_if_exe").ok();
    std::fs::remove_file("test_nested_if_exe.ll").ok();
}
