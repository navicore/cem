/**
 * Compile an echo program that reads a line and writes it back
 * Manually constructs AST for:
 * : echo ( -- ) read_line write_line ;
 */

use cem::ast::{Expr, Program, SourceLoc, WordDef};
use cem::ast::types::{Effect, StackType, Type};
use cem::codegen::{CodeGen, link_program};
use std::process::Command;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Build runtime first
    println!("Building runtime with I/O support...");
    let status = Command::new("just")
        .arg("build-runtime")
        .status()?;

    if !status.success() {
        return Err("Failed to build runtime".into());
    }

    // Manually construct AST for:
    // : echo ( -- ) read_line write_line ;
    let word = WordDef {
        name: "echo".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty,
        },
        body: vec![
            Expr::WordCall("read_line".to_string(), SourceLoc::unknown()),
            Expr::WordCall("write_line".to_string(), SourceLoc::unknown()),
        ],
        loc: SourceLoc::unknown(),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    // Generate LLVM IR with main() function
    println!("Generating LLVM IR...");
    let mut codegen = CodeGen::new();
    let ir = codegen.compile_program_with_main(&program, Some("echo"))?;

    // Write IR to file for inspection
    std::fs::write("echo.ll", &ir)?;
    println!("Wrote LLVM IR to echo.ll");

    // Link with runtime
    println!("Linking with runtime...");
    link_program(&ir, "runtime/libcem_runtime.a", "echo")?;

    println!("\n✅ Successfully compiled to ./echo");
    println!("Test it with: echo \"Hello\" | ./echo");

    Ok(())
}
