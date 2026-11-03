# Embedded C# and VB.NET

How the grammar exposes the C# and VB.NET inside a Web Forms page, and how a consumer should count or inject that code.

Code nuggets and server-script bodies are opaque ranges. The `Language` attribute of a directive is a `language_value` node, and the scanner labels each later nugget `c_sharp_code` or `vb_code`. The value may be quoted or unquoted and is matched without regard to case: `C#`, `cs`, and `CSharp` select C#, and `VB`, `vbs`, `VisualBasic`, and `VBScript` select VB. Any other value, an empty value, or no attribute at all leaves the label unchanged, and the label starts as `c_sharp_code`. A `language` attribute on an element, such as `<script language="VB" runat="server">`, is an ordinary attribute and does not change the label.

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
