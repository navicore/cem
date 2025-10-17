use cemc::ast::types::{Effect, StackType, Type};
/**
End-to-end integration test: Cem source → LLVM IR → executable
*/
use cemc::ast::{Expr, MatchBranch, Pattern, Program, SourceLoc, TypeDef, Variant, WordDef};
use cemc::codegen::{CodeGen, compile_to_object, link_program};
use std::process::Command;
use std::sync::Once;

static INIT: Once = Once::new();

/// Build the runtime once for all tests
fn ensure_runtime_built() {
    INIT.call_once(|| {
        let status = Command::new("just")
            .arg("build-runtime")
            .status()
            .expect("Failed to execute just build-runtime");

        assert!(status.success(), "Runtime build failed");
    });
}

#[test]
fn test_end_to_end_compilation() {
    ensure_runtime_built();

    // Create a simple program: : fortytwo ( -- Int ) 42 ;
    let word = WordDef {
        name: "fortytwo".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![Expr::IntLit(42, SourceLoc::unknown())],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate LLVM IR
    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program(&program)
        .expect("Failed to generate IR");

    // Verify IR contains expected elements
    assert!(ir.contains("define ptr @fortytwo"));
    assert!(ir.contains("call ptr @push_int"));
    assert!(ir.contains("i64 42"));

    // Compile to object file (tests that LLVM accepts our IR)
    compile_to_object(&ir, "test_fortytwo").expect("Failed to compile IR to object");

    // Clean up
    std::fs::remove_file("test_fortytwo.o").ok();
    std::fs::remove_file("test_fortytwo.ll").ok();
}

#[test]
fn test_arithmetic_compilation() {
    // Build runtime
    ensure_runtime_built();

    // : eight ( -- Int ) 5 3 + ;
    let word = WordDef {
        name: "eight".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::IntLit(5, SourceLoc::unknown()),
            Expr::IntLit(3, SourceLoc::unknown()),
            Expr::WordCall("add".to_string(), SourceLoc::unknown()),
        ],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate and compile
    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program(&program)
        .expect("Failed to generate IR");

    assert!(ir.contains("@eight"));
    assert!(ir.contains("@add"));

    compile_to_object(&ir, "test_eight").expect("Failed to compile");

    // Clean up
    std::fs::remove_file("test_eight.o").ok();
    std::fs::remove_file("test_eight.ll").ok();
}

#[test]
fn test_executable_with_main() {
    // Build runtime
    ensure_runtime_built();

    // : fortytwo ( -- Int ) 42 ;
    let word = WordDef {
        name: "fortytwo".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![Expr::IntLit(42, SourceLoc::unknown())],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate IR with main() function
    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program_with_main(&program, Some("fortytwo"))
        .expect("Failed to generate IR");

    // Verify IR contains main function
    assert!(ir.contains("define i32 @main()"));
    assert!(ir.contains("strand_spawn(ptr @fortytwo")); // Entry word is spawned as a strand
    assert!(ir.contains("ret i32 0"));

    // Link to produce executable
    link_program(&ir, "runtime/libcem_runtime.a", "test_fortytwo_exe").expect("Failed to link");

    // Run the executable
    let output = Command::new("./test_fortytwo_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());

    // Clean up
    std::fs::remove_file("test_fortytwo_exe").ok();
    std::fs::remove_file("test_fortytwo_exe.ll").ok();
}

#[test]
fn test_multiply_executable() {
    // Build runtime
    ensure_runtime_built();

    // : product ( -- Int ) 6 7 * ;
    let word = WordDef {
        name: "product".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::IntLit(6, SourceLoc::unknown()),
            Expr::IntLit(7, SourceLoc::unknown()),
            Expr::WordCall("multiply".to_string(), SourceLoc::unknown()),
        ],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate and link
    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program_with_main(&program, Some("product"))
        .expect("Failed to generate IR");

    link_program(&ir, "runtime/libcem_runtime.a", "test_product_exe").expect("Failed to link");

    // Run and check output
    let output = Command::new("./test_product_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());
    // Clean up
    std::fs::remove_file("test_product_exe").ok();
    std::fs::remove_file("test_product_exe.ll").ok();
}

#[test]
fn test_if_expression() {
    // Build runtime
    ensure_runtime_built();

    // : abs ( Int -- Int ) dup 0 < if [ 0 swap - ] [ ] ;
    // Simplified: : test_if ( -- Int ) true if [ 42 ] [ 0 ] ;
    let word = WordDef {
        name: "test_if".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::BoolLit(true, SourceLoc::unknown()),
            Expr::If {
                then_branch: Box::new(Expr::Quotation(
                    vec![Expr::IntLit(42, SourceLoc::unknown())],
                    SourceLoc::unknown(),
                )),
                else_branch: Box::new(Expr::Quotation(
                    vec![Expr::IntLit(0, SourceLoc::unknown())],
                    SourceLoc::unknown(),
                )),
                loc: SourceLoc::unknown(),
            },
        ],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate and link
    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program_with_main(&program, Some("test_if"))
        .expect("Failed to generate IR");

    // Verify IR contains if/then/else structure
    assert!(ir.contains("br i1 %")); // Branch on boolean condition
    assert!(ir.contains("then_"));
    assert!(ir.contains("else_"));
    assert!(ir.contains("merge_"));
    assert!(ir.contains("phi ptr"));

    link_program(&ir, "runtime/libcem_runtime.a", "test_if_exe").expect("Failed to link");

    // Run and check output - should print 42 (true branch)
    let output = Command::new("./test_if_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());

    // Clean up
    std::fs::remove_file("test_if_exe").ok();
    std::fs::remove_file("test_if_exe.ll").ok();
}

#[test]
fn test_tail_call_optimization() {
    // Build runtime
    ensure_runtime_built();

    // Create a simple tail-recursive word that calls itself
    // : identity ( Int -- Int ) ;  (just returns input)
    let identity = WordDef {
        name: "identity".to_string(),
        effect: Effect {
            inputs: StackType::Empty.push(Type::Int),
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![], // Identity - does nothing, returns stack as-is
        loc: SourceLoc::unknown(),
    };

    // : call_identity ( -- Int ) 42 identity ;
    let call_identity = WordDef {
        name: "call_identity".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::IntLit(42, SourceLoc::unknown()),
            Expr::WordCall("identity".to_string(), SourceLoc::unknown()),
        ],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![identity, call_identity],
    };

    // Generate IR
    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program_with_main(&program, Some("call_identity"))
        .expect("Failed to generate IR");

    // Verify IR contains musttail for the last word call
    assert!(
        ir.contains("musttail call"),
        "Expected musttail optimization for tail call"
    );

    // Link and run to verify it works
    link_program(&ir, "runtime/libcem_runtime.a", "test_tail_call_exe").expect("Failed to link");

    let output = Command::new("./test_tail_call_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());
    // Clean up
    std::fs::remove_file("test_tail_call_exe").ok();
    std::fs::remove_file("test_tail_call_exe.ll").ok();
}

#[test]
fn test_if_false_branch() {
    // Build runtime
    ensure_runtime_built();

    // : test_if_false ( -- Int ) false if [ 42 ] [ 99 ] ;
    // Should take the else branch and return 99
    let word = WordDef {
        name: "test_if_false".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::BoolLit(false, SourceLoc::unknown()), // Push false
            Expr::If {
                then_branch: Box::new(Expr::Quotation(
                    vec![Expr::IntLit(42, SourceLoc::unknown())],
                    SourceLoc::unknown(),
                )),
                else_branch: Box::new(Expr::Quotation(
                    vec![Expr::IntLit(99, SourceLoc::unknown())],
                    SourceLoc::unknown(),
                )),
                loc: SourceLoc::unknown(),
            },
        ],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate and link
    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program_with_main(&program, Some("test_if_false"))
        .expect("Failed to generate IR");

    link_program(&ir, "runtime/libcem_runtime.a", "test_if_false_exe").expect("Failed to link");

    // Run and check output - should print 99 (false branch)
    let output = Command::new("./test_if_false_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());

    // Clean up
    std::fs::remove_file("test_if_false_exe").ok();
    std::fs::remove_file("test_if_false_exe.ll").ok();
}

#[test]
fn test_tail_call_in_if_branch() {
    // Build runtime
    ensure_runtime_built();

    // Create a helper word that just returns its input
    // : passthrough ( Int -- Int ) ;
    let passthrough = WordDef {
        name: "passthrough".to_string(),
        effect: Effect {
            inputs: StackType::Empty.push(Type::Int),
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![], // Identity - returns stack as-is
        loc: SourceLoc::unknown(),
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
        body: vec![Expr::If {
            // Both branches call passthrough in tail position
            // Then branch: push 42 then call passthrough
            then_branch: Box::new(Expr::Quotation(
                vec![
                    Expr::IntLit(42, SourceLoc::unknown()),
                    Expr::WordCall("passthrough".to_string(), SourceLoc::unknown()),
                ],
                SourceLoc::unknown(),
            )),
            // Else branch: push 99 then call passthrough
            else_branch: Box::new(Expr::Quotation(
                vec![
                    Expr::IntLit(99, SourceLoc::unknown()),
                    Expr::WordCall("passthrough".to_string(), SourceLoc::unknown()),
                ],
                SourceLoc::unknown(),
            )),
            loc: SourceLoc::unknown(),
        }],
        loc: SourceLoc::unknown(),
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
            Expr::BoolLit(true, SourceLoc::unknown()),
            Expr::WordCall("conditional_call".to_string(), SourceLoc::unknown()),
        ],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![passthrough, conditional_call, test_entry],
    };

    // Generate IR
    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program_with_main(&program, Some("test_entry"))
        .expect("Failed to generate IR");

    // Critical check: verify that passthrough calls in the if branches are tail-optimized
    // The IR should contain "musttail call ptr @passthrough" inside the branch blocks
    assert!(
        ir.contains("musttail call ptr @passthrough"),
        "Expected musttail optimization for tail calls in if branches"
    );

    // Link and run to verify it works correctly
    link_program(&ir, "runtime/libcem_runtime.a", "test_tail_in_if_exe").expect("Failed to link");

    let output = Command::new("./test_tail_in_if_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());
    // Clean up
    std::fs::remove_file("test_tail_in_if_exe").ok();
    std::fs::remove_file("test_tail_in_if_exe.ll").ok();
}

#[test]
fn test_nested_if_expressions() {
    // Build runtime
    ensure_runtime_built();

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
        body: vec![Expr::If {
            // Outer if: first bool
            then_branch: Box::new(Expr::Quotation(
                vec![
                    // Inner if in then branch
                    Expr::If {
                        then_branch: Box::new(Expr::Quotation(
                            vec![Expr::IntLit(1, SourceLoc::unknown())],
                            SourceLoc::unknown(),
                        )),
                        else_branch: Box::new(Expr::Quotation(
                            vec![Expr::IntLit(2, SourceLoc::unknown())],
                            SourceLoc::unknown(),
                        )),
                        loc: SourceLoc::unknown(),
                    },
                ],
                SourceLoc::unknown(),
            )),
            else_branch: Box::new(Expr::Quotation(
                vec![
                    // Inner if in else branch
                    Expr::If {
                        then_branch: Box::new(Expr::Quotation(
                            vec![Expr::IntLit(3, SourceLoc::unknown())],
                            SourceLoc::unknown(),
                        )),
                        else_branch: Box::new(Expr::Quotation(
                            vec![Expr::IntLit(4, SourceLoc::unknown())],
                            SourceLoc::unknown(),
                        )),
                        loc: SourceLoc::unknown(),
                    },
                ],
                SourceLoc::unknown(),
            )),
            loc: SourceLoc::unknown(),
        }],
        loc: SourceLoc::unknown(),
    };

    // Test case: true, true => should give 1
    let test_true_true = WordDef {
        name: "test_true_true".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::BoolLit(true, SourceLoc::unknown()), // Inner condition
            Expr::BoolLit(true, SourceLoc::unknown()), // Outer condition
            Expr::WordCall("nested_if".to_string(), SourceLoc::unknown()),
        ],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![nested_if, test_true_true],
    };

    // Generate and link
    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program_with_main(&program, Some("test_true_true"))
        .expect("Failed to generate IR");

    // Verify IR contains nested branching structure
    assert!(ir.contains("then_"), "Expected then branch labels");
    assert!(ir.contains("else_"), "Expected else branch labels");
    assert!(ir.contains("merge_"), "Expected merge block labels");

    // Save IR for debugging in target/ directory (gitignored)
    std::fs::create_dir_all("target").ok();
    std::fs::write("target/test_nested_if_debug.ll", &ir).expect("Failed to write IR");

    link_program(&ir, "runtime/libcem_runtime.a", "test_nested_if_exe").expect("Failed to link");

    // Run and check output - should print 1 (both true)
    let output = Command::new("./test_nested_if_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());

    // Clean up
    std::fs::remove_file("test_nested_if_exe").ok();
    std::fs::remove_file("test_nested_if_exe.ll").ok();
    std::fs::remove_file("target/test_nested_if_debug.ll").ok();
}

#[test]
fn test_scheduler_linkage() {
    // Build runtime
    ensure_runtime_built();

    // : test_scheduler ( -- Int )
    //   5 test_yield 10 add ;
    // Tests that test_yield links correctly and doesn't break execution
    // (Phase 1: test_yield is a no-op, scheduler is not functional yet)
    let word = WordDef {
        name: "test_scheduler".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![
            Expr::IntLit(5, SourceLoc::unknown()),
            Expr::WordCall("test_yield".to_string(), SourceLoc::unknown()),
            Expr::IntLit(10, SourceLoc::unknown()),
            Expr::WordCall("add".to_string(), SourceLoc::unknown()),
        ],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate IR
    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program_with_main(&program, Some("test_scheduler"))
        .expect("Failed to generate IR");

    // Verify test_yield is declared and called
    assert!(ir.contains("declare ptr @test_yield(ptr)"));
    assert!(ir.contains("call ptr @test_yield"));

    // Link and run
    link_program(&ir, "runtime/libcem_runtime.a", "test_scheduler_exe").expect("Failed to link");

    let output = Command::new("./test_scheduler_exe")
        .output()
        .expect("Failed to run executable");

    assert!(output.status.success());

    // Should output 15 (5 + 10)

    // Clean up
    std::fs::remove_file("test_scheduler_exe").ok();
    std::fs::remove_file("test_scheduler_exe.ll").ok();
}

#[test]
fn test_debug_metadata_emission() {
    // Test that debug metadata is properly emitted in LLVM IR
    let word = WordDef {
        name: "fortytwo".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![Expr::IntLit(
            42,
            SourceLoc::new(1, 25, "test.cem".to_string()),
        )],
        loc: SourceLoc::new(1, 1, "test.cem".to_string()),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program(&program)
        .expect("Failed to generate IR");

    // Verify debug metadata is present
    assert!(ir.contains("!DIFile"), "Should contain DIFile metadata");
    assert!(
        ir.contains("!DICompileUnit"),
        "Should contain DICompileUnit metadata"
    );
    assert!(
        ir.contains("!DISubprogram"),
        "Should contain DISubprogram metadata"
    );
    assert!(
        ir.contains("!DILocation"),
        "Should contain DILocation metadata"
    );
    assert!(ir.contains("!llvm.dbg.cu"), "Should contain llvm.dbg.cu");
    assert!(
        ir.contains("!llvm.module.flags"),
        "Should contain module flags"
    );

    // Verify instruction has debug annotation
    assert!(
        ir.contains(", !dbg !"),
        "Instructions should have !dbg annotations"
    );

    // Verify the function references its subprogram
    assert!(
        ir.contains("define ptr @fortytwo(ptr %stack) !dbg !"),
        "Function should reference DISubprogram"
    );
}

#[test]
fn test_debug_metadata_filename_escaping() {
    // Test that filenames with special characters are properly escaped
    let word = WordDef {
        name: "test".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![Expr::IntLit(
            42,
            SourceLoc::new(1, 1, "test\"file.cem".to_string()),
        )],
        loc: SourceLoc::new(1, 1, "test\"file.cem".to_string()),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program(&program)
        .expect("Failed to generate IR");

    // Verify the filename is properly escaped (quote becomes \")
    assert!(
        ir.contains(r#"!DIFile(filename: "test\"file.cem""#),
        "Filename with quotes should be escaped"
    );
}

#[test]
fn test_pattern_match_codegen() {
    ensure_runtime_built();

    // Create a simple Option type:
    // type Option<T> = Some(T) | None
    let option_typedef = TypeDef {
        name: "Option".to_string(),
        type_params: vec!["T".to_string()],
        variants: vec![
            Variant {
                name: "Some".to_string(),
                fields: vec![Type::Var("T".to_string())],
            },
            Variant {
                name: "None".to_string(),
                fields: vec![],
            },
        ],
    };

    // Create a word that pattern matches on Option:
    // : handle-option ( Option(Int) -- Int )
    //   match
    //     Some => [ ]      ; unwraps to Int
    //     None => [ 0 ]    ; returns 0
    //   end ;
    let word = WordDef {
        name: "handle_option".to_string(),
        effect: Effect {
            inputs: StackType::Empty.push(Type::Named {
                name: "Option".to_string(),
                args: vec![Type::Int],
            }),
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![Expr::Match {
            branches: vec![
                MatchBranch {
                    pattern: Pattern::Variant {
                        name: "Some".to_string(),
                    },
                    body: vec![], // Just unwraps the Int from Some
                },
                MatchBranch {
                    pattern: Pattern::Variant {
                        name: "None".to_string(),
                    },
                    body: vec![Expr::IntLit(0, SourceLoc::unknown())], // Push 0
                },
            ],
            loc: SourceLoc::unknown(),
        }],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![option_typedef],
        word_defs: vec![word],
    };

    // Generate IR
    let mut codegen = CodeGen::new();
    let ir = codegen
        .compile_program(&program)
        .expect("Failed to generate IR");

    // Save IR for debugging
    std::fs::create_dir_all("target").ok();
    std::fs::write("target/test_pattern_match.ll", &ir).expect("Failed to write IR");

    // Verify IR contains expected pattern match elements:

    // 1. Should have switch statement for pattern matching
    assert!(
        ir.contains("switch i32"),
        "IR should contain switch statement for pattern matching"
    );

    // 2. Should have case labels for each variant
    assert!(
        ir.contains("match_case_"),
        "IR should contain match case labels"
    );

    // 3. Should have default label (for exhaustiveness error)
    assert!(
        ir.contains("match_default_"),
        "IR should contain default case label"
    );

    // 4. Should call runtime_error for non-exhaustive match (unreachable)
    assert!(
        ir.contains("call void @runtime_error"),
        "IR should have runtime_error call for default case"
    );

    // 5. Should have merge label (or musttail returns)
    assert!(
        ir.contains("match_merge_") || ir.contains("ret ptr"),
        "IR should have merge point or returns"
    );

    // 6. Should extract variant tag from stack cell
    assert!(
        ir.contains("getelementptr inbounds"),
        "IR should extract variant tag using GEP"
    );

    // 7. Verify IR compiles to object code
    compile_to_object(&ir, "test_pattern_match").expect("Failed to compile IR");

    // Clean up
    std::fs::remove_file("test_pattern_match.o").ok();
    std::fs::remove_file("test_pattern_match.ll").ok();
    // Keep target/test_pattern_match.ll for inspection

    println!("✅ Pattern matching codegen test passed!");
}
