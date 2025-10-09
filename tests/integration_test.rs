/// End-to-end integration tests
///
/// Tests that parse and type-check complete Cem programs

use cem::parser::Parser;
use cem::typechecker::TypeChecker;

#[test]
fn test_parse_and_typecheck_square() {
    let input = r#"
        : square ( Int -- Int )
          dup * ;
    "#;

    // Parse
    let mut parser = Parser::new(input);
    let program = parser.parse().expect("Parse failed");

    // Type check
    let mut checker = TypeChecker::new();
    let result = checker.check_program(&program);

    assert!(result.is_ok(), "Type check failed: {:?}", result.err());
}

#[test]
fn test_parse_and_typecheck_arithmetic() {
    let input = r#"
        : add-one ( Int -- Int )
          1 + ;

        : times-two ( Int -- Int )
          2 * ;

        : add-one-times-two ( Int -- Int )
          add-one times-two ;
    "#;

    let mut parser = Parser::new(input);
    let program = parser.parse().expect("Parse failed");

    let mut checker = TypeChecker::new();
    let result = checker.check_program(&program);

    assert!(result.is_ok(), "Type check failed: {:?}", result.err());
}

#[test]
fn test_parse_and_typecheck_option_handling() {
    let input = r#"
        : unwrap-or ( Option(Int) Int -- Int )
          swap match
            Some => [ swap drop ]
            None => [ ]
          end ;
    "#;

    let mut parser = Parser::new(input);
    let program = parser.parse().expect("Parse failed");

    let mut checker = TypeChecker::new();
    let result = checker.check_program(&program);

    assert!(result.is_ok(), "Type check failed: {:?}", result.err());
}

#[test]
fn test_type_error_detected() {
    // This should fail: trying to add boolean and int
    let input = r#"
        : bad ( Bool -- Int )
          true + ;
    "#;

    let mut parser = Parser::new(input);
    let program = parser.parse().expect("Parse failed");

    let mut checker = TypeChecker::new();
    let result = checker.check_program(&program);

    // Should be a type error
    assert!(result.is_err(), "Expected type error but got success");
}

#[test]
fn test_non_exhaustive_match() {
    // Missing None branch
    let input = r#"
        : get-value ( Option(Int) -- Int )
          match
            Some => [ ]
          end ;
    "#;

    let mut parser = Parser::new(input);
    let program = parser.parse().expect("Parse failed");

    let mut checker = TypeChecker::new();
    let result = checker.check_program(&program);

    // Should be non-exhaustive error
    assert!(result.is_err(), "Expected non-exhaustive error");
}

#[test]
fn test_polymorphic_word() {
    let input = r#"
        : over ( A B -- A B A )
          swap dup rot ;
    "#;

    let mut parser = Parser::new(input);
    let program = parser.parse().expect("Parse failed");

    let mut checker = TypeChecker::new();
    let result = checker.check_program(&program);

    assert!(result.is_ok(), "Type check failed: {:?}", result.err());
}

#[test]
fn test_multiple_words() {
    let input = r#"
        : double ( Int -- Int )
          2 * ;

        : quadruple ( Int -- Int )
          double double ;
    "#;

    let mut parser = Parser::new(input);
    let program = parser.parse().expect("Parse failed");

    let mut checker = TypeChecker::new();
    let result = checker.check_program(&program);

    assert!(result.is_ok(), "Type check failed: {:?}", result.err());
}

#[test]
fn test_parse_and_typecheck_literals() {
    let input = r#"
        : test-int ( -- Int )
          42 ;

        : test-bool ( -- Bool )
          true ;

        : test-string ( -- String )
          "hello" ;
    "#;

    let mut parser = Parser::new(input);
    let program = parser.parse().expect("Parse failed");

    let mut checker = TypeChecker::new();
    let result = checker.check_program(&program);

    assert!(result.is_ok(), "Type check failed: {:?}", result.err());
}
