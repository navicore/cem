/// Recursive descent parser for Cem

use crate::ast::types::{Effect, StackType, Type};
use crate::ast::{Expr, MatchBranch, Pattern, Program, TypeDef, Variant, WordDef};
use crate::parser::lexer::{Lexer, Token, TokenKind};
use std::fmt;

#[derive(Debug, Clone)]
pub struct ParseError {
    pub message: String,
    pub line: usize,
    pub column: usize,
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Parse error at {}:{}: {}", self.line, self.column, self.message)
    }
}

impl std::error::Error for ParseError {}

pub struct Parser {
    tokens: Vec<Token>,
    current: usize,
}

impl Parser {
    pub fn new(input: &str) -> Self {
        let mut lexer = Lexer::new(input);
        let tokens = lexer.tokenize();
        Parser { tokens, current: 0 }
    }

    pub fn parse(&mut self) -> Result<Program, ParseError> {
        let mut type_defs = Vec::new();
        let mut word_defs = Vec::new();

        while !self.is_at_end() {
            if self.check(&TokenKind::Type) {
                type_defs.push(self.parse_type_def()?);
            } else if self.check(&TokenKind::Colon) {
                word_defs.push(self.parse_word_def()?);
            } else {
                return Err(self.error("Expected 'type' or ':'"));
            }
        }

        Ok(Program {
            type_defs,
            word_defs,
        })
    }

    fn parse_type_def(&mut self) -> Result<TypeDef, ParseError> {
        self.consume(&TokenKind::Type, "Expected 'type'")?;

        let name = self.consume_ident("Expected type name")?;

        // Optional type parameters
        let mut type_params = Vec::new();
        if self.check(&TokenKind::LeftParen) {
            self.advance();
            while !self.check(&TokenKind::RightParen) && !self.is_at_end() {
                type_params.push(self.consume_ident("Expected type parameter")?);
                if self.check(&TokenKind::RightParen) {
                    break;
                }
            }
            self.consume(&TokenKind::RightParen, "Expected ')'")?;
        }

        self.consume(&TokenKind::Pipe, "Expected '|' before first variant")?;

        // Parse variants
        let mut variants = Vec::new();
        loop {
            let variant_name = self.consume_ident("Expected variant name")?;

            // Parse variant fields (optional)
            let mut fields = Vec::new();
            if self.check(&TokenKind::LeftParen) {
                self.advance();
                while !self.check(&TokenKind::RightParen) && !self.is_at_end() {
                    fields.push(self.parse_type()?);
                    if self.check(&TokenKind::RightParen) {
                        break;
                    }
                }
                self.consume(&TokenKind::RightParen, "Expected ')'")?;
            }

            variants.push(Variant {
                name: variant_name,
                fields,
            });

            // Check for more variants
            if self.check(&TokenKind::Pipe) {
                self.advance();
            } else {
                break;
            }
        }

        Ok(TypeDef {
            name,
            type_params,
            variants,
        })
    }

    fn parse_word_def(&mut self) -> Result<WordDef, ParseError> {
        self.consume(&TokenKind::Colon, "Expected ':'")?;

        let name = self.consume_ident("Expected word name")?;

        // Parse effect signature
        self.consume(&TokenKind::LeftParen, "Expected '(' for effect signature")?;
        let effect = self.parse_effect()?;
        self.consume(&TokenKind::RightParen, "Expected ')' after effect signature")?;

        // Parse body until ';'
        let mut body = Vec::new();
        while !self.check_ident(";") && !self.is_at_end() {
            body.push(self.parse_expr()?);
        }

        self.consume_ident_value(";", "Expected ';' at end of word definition")?;

        Ok(WordDef { name, effect, body })
    }

    fn parse_effect(&mut self) -> Result<Effect, ParseError> {
        // Parse input stack types
        let mut inputs = Vec::new();
        while !self.check(&TokenKind::Dash) && !self.is_at_end() {
            inputs.push(self.parse_type()?);
        }

        self.consume(&TokenKind::Dash, "Expected '--' in effect signature")?;

        // Parse output stack types
        let mut outputs = Vec::new();
        while !self.check(&TokenKind::RightParen) && !self.is_at_end() {
            outputs.push(self.parse_type()?);
        }

        Ok(Effect::from_vecs(inputs, outputs))
    }

    fn parse_type(&mut self) -> Result<Type, ParseError> {
        let name = self.consume_ident("Expected type name")?;

        match name.as_str() {
            "Int" => Ok(Type::Int),
            "Bool" => Ok(Type::Bool),
            "String" => Ok(Type::String),
            _ => {
                // Check if it's a generic type variable (single uppercase letter or starts with lowercase)
                if name.len() == 1 && name.chars().next().unwrap().is_uppercase() {
                    Ok(Type::Var(name))
                } else if name.chars().next().unwrap().is_lowercase() {
                    Ok(Type::Var(name))
                } else {
                    // Named type, possibly with type arguments
                    let args = if self.check(&TokenKind::LeftParen) {
                        self.advance();
                        let mut args = Vec::new();
                        while !self.check(&TokenKind::RightParen) && !self.is_at_end() {
                            args.push(self.parse_type()?);
                            if self.check(&TokenKind::RightParen) {
                                break;
                            }
                        }
                        self.consume(&TokenKind::RightParen, "Expected ')'")?;
                        args
                    } else {
                        Vec::new()
                    };

                    Ok(Type::Named { name, args })
                }
            }
        }
    }

    fn parse_expr(&mut self) -> Result<Expr, ParseError> {
        let token = self.peek();

        match &token.kind {
            TokenKind::IntLiteral => {
                let value = token.lexeme.parse::<i64>().map_err(|_| {
                    ParseError {
                        message: format!("Invalid integer: {}", token.lexeme),
                        line: token.line,
                        column: token.column,
                    }
                })?;
                self.advance();
                Ok(Expr::IntLit(value))
            }

            TokenKind::BoolLiteral => {
                let value = token.lexeme == "true";
                self.advance();
                Ok(Expr::BoolLit(value))
            }

            TokenKind::StringLiteral => {
                let value = token.lexeme.clone();
                self.advance();
                Ok(Expr::StringLit(value))
            }

            TokenKind::LeftBracket => {
                self.advance(); // consume '['
                let mut exprs = Vec::new();
                while !self.check(&TokenKind::RightBracket) && !self.is_at_end() {
                    exprs.push(self.parse_expr()?);
                }
                self.consume(&TokenKind::RightBracket, "Expected ']'")?;
                Ok(Expr::Quotation(exprs))
            }

            TokenKind::Match => {
                self.advance(); // consume 'match'
                let mut branches = Vec::new();

                while !self.check(&TokenKind::End) && !self.is_at_end() {
                    let variant_name = self.consume_ident("Expected variant name")?;
                    self.consume(&TokenKind::Arrow, "Expected '=>'")?;

                    // Parse branch body (quotation)
                    self.consume(&TokenKind::LeftBracket, "Expected '[' for branch body")?;
                    let mut body = Vec::new();
                    while !self.check(&TokenKind::RightBracket) && !self.is_at_end() {
                        body.push(self.parse_expr()?);
                    }
                    self.consume(&TokenKind::RightBracket, "Expected ']'")?;

                    branches.push(MatchBranch {
                        pattern: Pattern::Variant { name: variant_name },
                        body,
                    });
                }

                self.consume(&TokenKind::End, "Expected 'end'")?;
                Ok(Expr::Match { branches })
            }

            TokenKind::If => {
                self.advance(); // consume 'if'

                // Expect two quotations: then-branch and else-branch
                self.consume(&TokenKind::LeftBracket, "Expected '[' for then branch")?;
                let mut then_exprs = Vec::new();
                while !self.check(&TokenKind::RightBracket) && !self.is_at_end() {
                    then_exprs.push(self.parse_expr()?);
                }
                self.consume(&TokenKind::RightBracket, "Expected ']'")?;

                self.consume(&TokenKind::LeftBracket, "Expected '[' for else branch")?;
                let mut else_exprs = Vec::new();
                while !self.check(&TokenKind::RightBracket) && !self.is_at_end() {
                    else_exprs.push(self.parse_expr()?);
                }
                self.consume(&TokenKind::RightBracket, "Expected ']'")?;

                Ok(Expr::If {
                    then_branch: Box::new(Expr::Quotation(then_exprs)),
                    else_branch: Box::new(Expr::Quotation(else_exprs)),
                })
            }

            TokenKind::While => {
                self.advance(); // consume 'while'

                // Expect two quotations: condition and body
                self.consume(&TokenKind::LeftBracket, "Expected '[' for condition")?;
                let mut cond_exprs = Vec::new();
                while !self.check(&TokenKind::RightBracket) && !self.is_at_end() {
                    cond_exprs.push(self.parse_expr()?);
                }
                self.consume(&TokenKind::RightBracket, "Expected ']'")?;

                self.consume(&TokenKind::LeftBracket, "Expected '[' for body")?;
                let mut body_exprs = Vec::new();
                while !self.check(&TokenKind::RightBracket) && !self.is_at_end() {
                    body_exprs.push(self.parse_expr()?);
                }
                self.consume(&TokenKind::RightBracket, "Expected ']'")?;

                Ok(Expr::While {
                    condition: Box::new(Expr::Quotation(cond_exprs)),
                    body: Box::new(Expr::Quotation(body_exprs)),
                })
            }

            TokenKind::Ident => {
                let name = token.lexeme.clone();
                self.advance();
                Ok(Expr::WordCall(name))
            }

            _ => Err(ParseError {
                message: format!("Unexpected token: {:?}", token.kind),
                line: token.line,
                column: token.column,
            }),
        }
    }

    // Helper methods

    fn peek(&self) -> &Token {
        &self.tokens[self.current]
    }

    fn is_at_end(&self) -> bool {
        self.peek().kind == TokenKind::Eof
    }

    fn advance(&mut self) -> &Token {
        if !self.is_at_end() {
            self.current += 1;
        }
        &self.tokens[self.current - 1]
    }

    fn check(&self, kind: &TokenKind) -> bool {
        if self.is_at_end() {
            return false;
        }
        &self.peek().kind == kind
    }

    fn check_ident(&self, value: &str) -> bool {
        if self.is_at_end() {
            return false;
        }
        let token = self.peek();
        token.kind == TokenKind::Ident && token.lexeme == value
    }

    fn consume(&mut self, kind: &TokenKind, message: &str) -> Result<&Token, ParseError> {
        if self.check(kind) {
            Ok(self.advance())
        } else {
            Err(self.error(message))
        }
    }

    fn consume_ident(&mut self, message: &str) -> Result<String, ParseError> {
        if self.peek().kind == TokenKind::Ident {
            let lexeme = self.peek().lexeme.clone();
            self.advance();
            Ok(lexeme)
        } else {
            Err(self.error(message))
        }
    }

    fn consume_ident_value(&mut self, value: &str, message: &str) -> Result<(), ParseError> {
        if self.check_ident(value) {
            self.advance();
            Ok(())
        } else {
            Err(self.error(message))
        }
    }

    fn error(&self, message: &str) -> ParseError {
        let token = self.peek();
        ParseError {
            message: message.to_string(),
            line: token.line,
            column: token.column,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_simple_word() {
        let input = ": square ( Int -- Int ) dup * ;";
        let mut parser = Parser::new(input);
        let program = parser.parse().unwrap();

        assert_eq!(program.word_defs.len(), 1);
        assert_eq!(program.word_defs[0].name, "square");
        assert_eq!(program.word_defs[0].body.len(), 2); // dup, *
    }

    #[test]
    fn test_parse_type_def() {
        let input = "type Option (T) | Some(T) | None";
        let mut parser = Parser::new(input);
        let program = parser.parse().unwrap();

        assert_eq!(program.type_defs.len(), 1);
        assert_eq!(program.type_defs[0].name, "Option");
        assert_eq!(program.type_defs[0].type_params.len(), 1);
        assert_eq!(program.type_defs[0].variants.len(), 2);
    }

    #[test]
    fn test_parse_literals() {
        let input = ": test ( -- Int ) 42 ;";
        let mut parser = Parser::new(input);
        let program = parser.parse().unwrap();

        assert_eq!(program.word_defs[0].body.len(), 1);
        match &program.word_defs[0].body[0] {
            Expr::IntLit(42) => (),
            _ => panic!("Expected IntLit(42)"),
        }
    }

    #[test]
    fn test_parse_quotation() {
        let input = ": test ( -- ) [ 1 2 + ] ;";
        let mut parser = Parser::new(input);
        let program = parser.parse().unwrap();

        assert_eq!(program.word_defs[0].body.len(), 1);
        match &program.word_defs[0].body[0] {
            Expr::Quotation(exprs) => assert_eq!(exprs.len(), 3),
            _ => panic!("Expected Quotation"),
        }
    }
}
