# Embedded C# and VB.NET

How the grammar exposes the C# and VB.NET inside a Web Forms page, and how a consumer should count or inject that code.

Code nuggets and server-script bodies are opaque ranges. The `Language` attribute of a directive is a `language_value` node, and the scanner labels each later nugget `c_sharp_code` or `vb_code`. The value may be quoted or unquoted and is matched without regard to case: `C#`, `cs`, and `CSharp` select C#, and `VB`, `vbs`, `VisualBasic`, and `VBScript` select VB. Any other value, an empty value, or no attribute at all leaves the label unchanged, and the label starts as `c_sharp_code`. A `language` attribute on an element, such as `<script language="VB" runat="server">`, is an ordinary attribute and does not change the label.

| Node | What it is |
| --- | --- |
| `encoded_expression`, `output_expression`, `binding_expression` | `<%: %>`, `<%= %>`, `<%# %>` |
| `statement_block` | `<% %>` |
| `expression_builder` / `expression_value` | `<%$ %>`, not C# or VB |
| `server_comment` | `<%-- --%>`, not code |
| `c_sharp_code`, `vb_code` | The bytes inside a nugget or a `<script>` body. A nugget inside the body splits it into several chunks |
| `handler_code` | The code of a `WebHandler` or `WebService` file, as a `c_sharp_code` or `vb_code` child labeled with the directive's language |

`queries/injections.scm` maps those code nodes to `c_sharp` or `vb`. Statement blocks set `injection.combined`, so `<% if (ready) { %>` … `<% } %>` is one injected document and the markup between them is not part of it. Output, encoded, and binding nuggets are injected one by one. A `<script>` whose start tag has `runat="server"` keeps the page language; the value matches in any casing and quoted or not, as ASP.NET compares it. Any other `<script>` body is injected as JavaScript, even though the node is still `c_sharp_code` or `vb_code`. `<style>` bodies are injected as CSS. A nugget inside a `<script>` or `<style>` body, such as `var id = '<%= Ctl.ClientID %>';`, is a real nugget node; the body chunks around it are combined into one injected script or stylesheet, and the nugget's own code is injected as C# or VB. Nuggets that stand in for attributes in a tag (`<option <%= selected %>>`) are injected the same way. The body scan stops at a `<%` only when a `%>` closes it before the body's end tag, so an unclosed `<%` in a server script's string literal stays code; a complete `<%= ... %>` inside such a string still becomes a nugget node. Combined injection joins every match of a pattern in the page, so all client `<script>` bodies on a page are one injected JavaScript document, all server script bodies are one C# or VB document, and all `<style>` bodies are one stylesheet.

A consumer that counts executable code should count `c_sharp_code` and `vb_code` bytes, and should skip the body chunks of a `<script>` whose start tag has no `runat="server"`: the `c_sharp_code` and `vb_code` nodes that are direct children of that `script_element`. Nuggets inside such a script, such as `var id = '<%= Ctl.ClientID %>';`, are server code and are counted like any other nugget. Markup, attribute text, style text, expression-builder text, and server comments are not executable code. Server comments are their own node kind.

`.ashx` and `.asmx` files are a `WebHandler` or `WebService` directive followed by code. A line that starts, after optional indentation, with a directive (such as `<%@ Assembly Name="..." %>`), a complete server comment, or a `<script runat="server">` element that has a later `</script>` begins that construct. Everything else is `handler_code`, including HTML that a handler writes from a C# or VB string, since a client `<script>` there has no `runat`; its `c_sharp_code` or `vb_code` child is counted and injected like nugget code. Leading whitespace before the code is not part of it. `.asax` stays markup.

Some legacy pages are UTF-16. Decode them to UTF-8 before parsing. `tools/corpus-check.mjs` does that.
