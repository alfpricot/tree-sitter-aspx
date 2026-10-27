# tree-sitter-aspx

[![Verify](https://github.com/alfpricot/tree-sitter-aspx/actions/workflows/verify.yml/badge.svg?branch=dev)](https://github.com/alfpricot/tree-sitter-aspx/actions/workflows/verify.yml)
[![Crates.io](https://img.shields.io/crates/v/tree-sitter-aspx)](https://crates.io/crates/tree-sitter-aspx)

A [tree-sitter](https://tree-sitter.github.io/tree-sitter/) grammar for ASP.NET Web Forms markup: `.aspx` pages, `.ascx` user controls, `.master` master pages, and the directive headers of `.asax`, `.asmx`, and `.ashx` files, with Node and Rust bindings. Classic ASP (`.asp` with VBScript) and Razor (`.cshtml`, `.vbhtml`) are different languages and are out of scope, and code-behind files (`.aspx.cs`, `.aspx.vb`) are ordinary C# or VB.NET that this grammar does not parse.

## Installation

```sh
npm install tree-sitter-aspx   # Node
cargo add tree-sitter-aspx     # Rust
```

### Requirements

- **Node:** the `tree-sitter` package, version `^0.25.0`, is an optional peer dependency. The binding is a Node-API addon with prebuilt copies for Linux and Windows; on any other platform the install compiles it from source and needs a C/C++ toolchain that `node-gyp` can use.
- **Rust:** 1.85 or newer. The crate depends only on `tree-sitter-language`, so the `tree-sitter` version your own crate uses is the one that parses.

## Usage

The grammar parses directives, server comments, HTML comments, code nuggets, `<script>` and `<style>` elements, and tags with attributes. It does not validate HTML. Void tags (`br`, `img`, `meta`, and the other HTML void elements) do not swallow the following markup. An end tag closes the nearest open element; the names are not required to match. `<script>` and `<style>` bodies are raw text, so `<` inside JavaScript or CSS is not a tag.

The C# and VB.NET inside a page are exposed as `c_sharp_code` and `vb_code` nodes, with injection queries for editors. See [docs/EMBEDDED_CODE.md](docs/EMBEDDED_CODE.md).

### Node

The default export exposes `name`, `language`, and `nodeTypeInfo` (the entries from `src/node-types.json`) on every platform, plus the `HIGHLIGHTS_QUERY` and `INJECTIONS_QUERY` strings.

```js
import Parser from 'tree-sitter';
import Aspx from 'tree-sitter-aspx';

const parser = new Parser();
parser.setLanguage(Aspx);
const tree = parser.parse('<%@ Page Language="C#" %>');
console.log(tree.rootNode.toString());
```

### Rust

```rust
let mut parser = tree_sitter::Parser::new();
parser
    .set_language(&tree_sitter_aspx::LANGUAGE.into())
    .expect("Error loading ASPX parser");
```

`LANGUAGE` is a `tree_sitter_language::LanguageFn`. The crate also exports `NODE_TYPES`, `HIGHLIGHTS_QUERY`, and `INJECTIONS_QUERY`.

## How to contribute

Branch from `dev` and open pull requests against it. [CONTRIBUTING.md](CONTRIBUTING.md) covers building the parser, running the checks CI runs, and the pull request rules; [ARCHITECTURE.md](ARCHITECTURE.md) explains how the pieces fit together.

## License

MIT. See [LICENSE](LICENSE).
