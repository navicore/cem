use cemc::ast::types::{Effect, StackType, Type};
/**
 * Temporary program to compile hello_io.cem
 * Manually constructs AST for: : main ( -- ) "Hello, World!" write_line ;
 */
use cemc::ast::{Expr, Program, SourceLoc, WordDef};
use cemc::codegen::{CodeGen, link_program};
use std::process::Command;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Build runtime first
    println!("Building runtime with I/O support...");
    let status = Command::new("just").arg("build-runtime").status()?;

    if !status.success() {
        return Err("Failed to build runtime".into());
    }

    // Manually construct AST for:
    // : main ( -- ) "Hello, World!" write_line ;
    let word = WordDef {
        name: "main".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty,
        },
        body: vec![
            Expr::StringLit("Hello, World!".to_string(), SourceLoc::unknown()),
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
    let ir = codegen.compile_program_with_main(&program, Some("main"))?;

    // Write IR to file for inspection
    std::fs::write("hello_io.ll", &ir)?;
    println!("Wrote LLVM IR to hello_io.ll");

    // Link with runtime (link_program compiles IR and links in one step)
    println!("Linking with runtime...");
    link_program(&ir, "runtime/libcem_runtime.a", "hello_io")?;

    println!("\n✅ Successfully compiled to ./hello_io");
    println!("Run it with: ./hello_io");

    Ok(())
}
