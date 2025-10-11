use cem::ast::{Expr, Program, SourceLoc, WordDef};
use cem::ast::types::{Effect, StackType, Type};
use cem::codegen::CodeGen;

fn main() {
    let word = WordDef {
        name: "fortytwo".to_string(),
        effect: Effect {
            inputs: StackType::Empty,
            outputs: StackType::Empty.push(Type::Int),
        },
        body: vec![Expr::IntLit(42, SourceLoc::new(1, 25, "test.cem".to_string()))],
        loc: SourceLoc::new(1, 1, "test.cem".to_string()),
    };

    let program = Program {
        type_defs: vec![],
        word_defs: vec![word],
    };

    let mut codegen = CodeGen::new();
    let ir = codegen.compile_program(&program).expect("Failed to generate IR");

    println!("Generated IR:\n{}", ir);

    // Verify debug metadata is present
    assert!(ir.contains("!DIFile"), "Should contain DIFile metadata");
    assert!(ir.contains("!DICompileUnit"), "Should contain DICompileUnit metadata");
    assert!(ir.contains("!llvm.dbg.cu"), "Should contain llvm.dbg.cu");
    assert!(ir.contains("!llvm.module.flags"), "Should contain module flags");

    println!("\n✅ Debug metadata successfully emitted!");
}
