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

The grammar parses directives, server comments, HTML comments, code nuggets, `<script>` and `<style>` elements, and tags with attributes. It does not validate HTML. Void tags (`br`, `img`, `meta`, and the other HTML void elements) do not swallow the following markup. End tags that HTML lets a page leave out are implied, as a browser would: an open `li`, `p`, `td`, `tr`, `option`, `dt`, `dd`, or other element with an optional end tag ends at the start tag that closes it (a sibling `li`, a block element after a `p`, and so on), at an ancestor's end tag, or at the end of the file. An end tag that names an element further out closes the elements inside it, and an end tag whose name matches no open element closes the innermost one. `<script>` and `<style>` bodies are raw text, so `<` inside JavaScript or CSS is not a tag, but a nugget or server comment inside a body is still parsed as one. Nuggets and server comments may also stand in for attributes in a tag, as in `<option <%= selected %>>`. `<![CDATA[ ]]>` sections and `<?xml ?>` declarations parse as `cdata_section` and `processing_instruction` nodes whose nuggets are still parsed, and self-closing `<script ... />` and `<style />` tags are `self_closing_element` nodes.

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
