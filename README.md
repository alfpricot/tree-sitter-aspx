# tree-sitter-aspx

A [tree-sitter](https://tree-sitter.github.io/tree-sitter/) grammar for ASP.NET Web Forms markup: `.aspx` pages, `.ascx` user controls, `.master` master pages, and the directive headers of `.asax`, `.asmx`, and `.ashx` files.

Classic ASP (`.asp` with VBScript) and Razor (`.cshtml`, `.vbhtml`) are different languages and are out of scope. Code-behind files (`.aspx.cs`, `.aspx.vb`) are ordinary C# or VB.NET and are not parsed by this grammar.

The grammar parses directives, server comments, HTML comments, code nuggets, `<script>` and `<style>` elements, and tags with attributes. It does not validate HTML. Void tags (`br`, `img`, `meta`, and the other HTML void elements) do not swallow the following markup. An end tag closes the nearest open element; the names are not required to match. `<script>` and `<style>` bodies are raw text, so `<` inside JavaScript or CSS is not a tag.

## Embedded C# and VB.NET

Code nuggets and server-script bodies are opaque ranges. The page `Language` attribute (`C#` or `VB`, any casing) is a `language_value` node, and the scanner labels each later nugget `c_sharp_code` or `vb_code`. When the attribute is absent, the label is `c_sharp_code`.

| Node | What it is |
| --- | --- |
| `encoded_expression`, `output_expression`, `binding_expression` | `<%: %>`, `<%= %>`, `<%# %>` |
| `statement_block` | `<% %>` |
| `expression_builder` / `expression_value` | `<%$ %>`, not C# or VB |
| `server_comment` | `<%-- --%>`, not code |
| `c_sharp_code`, `vb_code` | The bytes inside a nugget or a `<script>` body |
| `handler_code` | Everything after a `WebHandler` or `WebService` directive that is not a `<script>` element |

`queries/injections.scm` maps those code nodes to `c_sharp` or `vb`. Statement blocks set `injection.combined`, so `<% if (ready) { %>` … `<% } %>` is one injected document and the markup between them is not part of it. Output, encoded, and binding nuggets are injected one by one. A `<script>` whose start tag has `runat="server"` keeps the page language. Any other `<script>` body is injected as JavaScript, even though the node is still `c_sharp_code` or `vb_code`. `<style>` bodies are injected as CSS.

A consumer that counts executable code should count `c_sharp_code` and `vb_code` bytes, and should skip a `<script>` body whose start tag has no `runat="server"`. Markup, attribute text, style text, expression-builder text, and server comments are not executable code. Server comments are their own node kind.

`.ashx` and `.asmx` files are a `WebHandler` or `WebService` directive plus either a `<script runat="server">` element or the rest of the file as `handler_code`. `.asax` stays markup.

Some legacy pages are UTF-16. Decode them to UTF-8 before parsing. `tools/corpus-check.mjs` does that.

## Node package metadata

The default export exposes `name`, `language`, and `nodeTypeInfo` on Linux and Windows. `nodeTypeInfo` contains the entries from `src/node-types.json`.

```js
import Parser from 'tree-sitter';
import Aspx from 'tree-sitter-aspx';

const parser = new Parser();
parser.setLanguage(Aspx);
const tree = parser.parse('<%@ Page Language="C#" %>');
console.log(tree.rootNode.toString());
```

## Rust

```rust
let mut parser = tree_sitter::Parser::new();
parser
    .set_language(&tree_sitter_aspx::LANGUAGE.into())
    .expect("Error loading ASPX parser");
```

`LANGUAGE` is a `tree_sitter_language::LanguageFn`. The crate's runtime dependency is `tree-sitter-language`, so the consuming crate's own `tree-sitter` version is what parses.

## Building

```sh
npm ci --ignore-scripts
npm rebuild tree-sitter-cli
npx --no-install tree-sitter generate
cargo build
```

Release jobs require a version tag on `main` matching the Cargo package version and publish from the commit that passed verification. The npm and GitHub Packages archives carry that same version in both `package.json` and `tree-sitter.json`.

Publishing waits on the registry credentials configured for this repository. Until the first tag is published, depend on a git revision of `dev`.
