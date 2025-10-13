use cemc::codegen::{CodeGen, link_program};
use cemc::parser::Parser;
use std::fs;
use std::path::Path;
use std::process::Command;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();

    if args.len() < 3 {
        eprintln!("Usage: cem compile <input.cem>");
        eprintln!("       cem compile <input.cem> -o <output>");
        std::process::exit(1);
    }

    let command = &args[1];
    if command != "compile" {
        eprintln!("Unknown command: {}", command);
        eprintln!("Available commands: compile");
        std::process::exit(1);
    }

    let input_file = &args[2];

    // Determine output name
    let output_name = if args.len() >= 5 && args[3] == "-o" {
        args[4].clone()
    } else {
        // Default: strip .cem extension and use as output name
        Path::new(input_file)
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("output")
            .to_string()
    };

    // Read source file
    let source = fs::read_to_string(input_file)
        .map_err(|e| format!("Failed to read {}: {}", input_file, e))?;

    // Parse
    println!("Parsing {}...", input_file);
    let mut parser = Parser::new_with_filename(&source, input_file);
    let program = parser.parse().map_err(|e| format!("Parse error: {}", e))?;

    // Build runtime first
    println!("Building runtime...");
    let status = Command::new("just").arg("build-runtime").status()?;

    if !status.success() {
        return Err("Failed to build runtime".into());
    }

    // Generate LLVM IR
    println!("Generating LLVM IR...");
    let mut codegen = CodeGen::new();

    // Find entry point (look for "main" word, or use first word if only one)
    let has_main = program.word_defs.iter().any(|w| w.name == "main");
    let entry_word = if has_main {
        Some("main")
    } else if program.word_defs.len() == 1 {
        println!(
            "Note: Using '{}' as entry point (no 'main' word found)",
            program.word_defs[0].name
        );
        Some(program.word_defs[0].name.as_str())
    } else {
        eprintln!("Error: No 'main' word found and multiple words defined");
        eprintln!("Either define a 'main' word or compile a file with only one word");
        std::process::exit(1);
    };

    let ir = codegen.compile_program_with_main(&program, entry_word)?;

    // Write IR to file for debugging
    let ir_file = format!("{}.ll", output_name);
    fs::write(&ir_file, &ir)?;
    println!("Wrote LLVM IR to {}", ir_file);

    // Link with runtime
    println!("Linking...");
    link_program(&ir, "runtime/libcem_runtime.a", &output_name)?;

    println!("\n✅ Successfully compiled to ./{}", output_name);
    println!("Run it with: ./{}", output_name);

    Ok(())
}
