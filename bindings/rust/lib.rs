//! This crate provides ASP.NET Web Forms markup support for the [tree-sitter][]
//! parsing library.
//!
//! Typically, you will use the [LANGUAGE][] constant to add this language to a
//! tree-sitter [Parser][], and then use the parser to parse some markup:
//!
//! ```
//! let code = r#"<%@ Page Language="C#" %>"#;
//! let mut parser = tree_sitter::Parser::new();
//! let language = tree_sitter_aspx::LANGUAGE;
//! parser
//!     .set_language(&language.into())
//!     .expect("Error loading ASPX parser");
//! let tree = parser.parse(code, None).unwrap();
//! assert!(!tree.root_node().has_error());
//! ```
//!
//! [Parser]: https://docs.rs/tree-sitter/*/tree_sitter/struct.Parser.html
//! [tree-sitter]: https://tree-sitter.github.io/

use tree_sitter_language::LanguageFn;

// Edition 2024 requires `extern` blocks to be marked `unsafe`: declaring a
// foreign symbol is itself the unsafe act, since nothing checks that the
// signature here matches the one the generated parser actually exports.
unsafe extern "C" {
    fn tree_sitter_aspx() -> *const ();
}

/// The tree-sitter [`LanguageFn`][LanguageFn] for this grammar.
///
/// [LanguageFn]: https://docs.rs/tree-sitter-language/*/tree_sitter_language/struct.LanguageFn.html
pub const LANGUAGE: LanguageFn = unsafe { LanguageFn::from_raw(tree_sitter_aspx) };

/// The content of the [`node-types.json`][] file for this grammar.
///
/// [`node-types.json`]: https://tree-sitter.github.io/tree-sitter/using-parsers#static-node-types
pub const NODE_TYPES: &str = include_str!("../../src/node-types.json");

pub const HIGHLIGHTS_QUERY: &str = include_str!("../../queries/highlights.scm");
pub const INJECTIONS_QUERY: &str = include_str!("../../queries/injections.scm");

/// The exported C symbol name, `tree_sitter_aspx`. This constant pins the
/// name so a rename cannot land quietly; the test below asserts it against
/// the generated parser's source.
pub const SYMBOL_NAME: &str = "tree_sitter_aspx";

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_can_load_grammar() {
        let mut parser = tree_sitter::Parser::new();
        parser
            .set_language(&LANGUAGE.into())
            .expect("Error loading ASPX parser");
    }

    /// The exported symbol must be `tree_sitter_aspx` — see SYMBOL_NAME.
    /// Checking the generated parser source is the cheapest reliable
    /// assertion: the symbol is emitted there as the parser's single
    /// entry point, and the extern declaration above fails to link under
    /// any other name.
    #[test]
    fn test_exports_the_expected_symbol() {
        let parser_c = include_str!("../../src/parser.c");
        assert!(
            parser_c.contains("tree_sitter_aspx(void)"),
            "generated parser no longer exports tree_sitter_aspx"
        );
    }

    #[test]
    fn test_parses_a_page_directive() {
        let mut parser = tree_sitter::Parser::new();
        parser.set_language(&LANGUAGE.into()).unwrap();
        let tree = parser.parse(r#"<%@ Page Language="C#" %>"#, None).unwrap();
        assert!(!tree.root_node().has_error());
    }
}
