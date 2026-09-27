; Embedded C# and VB.NET.
;
; The scanner records the page Language attribute and emits c_sharp_code or
; vb_code for each nugget, so a host does not have to walk back to the
; directive. Statement blocks are combined: `<% if (x) { %>` markup `<% } %>`
; is one C# or VB document with the markup left out of the injected ranges.
; Output, encoded, and binding nuggets are separate expressions and are not
; combined with statement blocks or with each other.
;
; A <script> body uses the same code node. Without runat="server" the body
; is client script, so the query below replaces the page language with
; JavaScript. With runat="server" the page language stands.
; Expression builders (`<%$ ... %>`) are not C# or VB and are not injected.
; <style> bodies inject as CSS.

((statement_block
   (c_sharp_code) @injection.content)
 (#set! injection.language "c_sharp")
 (#set! injection.combined))

((statement_block
   (vb_code) @injection.content)
 (#set! injection.language "vb")
 (#set! injection.combined))

((output_expression
   (c_sharp_code) @injection.content)
 (#set! injection.language "c_sharp"))

((output_expression
   (vb_code) @injection.content)
 (#set! injection.language "vb"))

((encoded_expression
   (c_sharp_code) @injection.content)
 (#set! injection.language "c_sharp"))

((encoded_expression
   (vb_code) @injection.content)
 (#set! injection.language "vb"))

((binding_expression
   (c_sharp_code) @injection.content)
 (#set! injection.language "c_sharp"))

((binding_expression
   (vb_code) @injection.content)
 (#set! injection.language "vb"))

((script_element
   (start_tag) @_tag
   (c_sharp_code) @injection.content)
 (#match? @_tag "[Rr][Uu][Nn][Aa][Tt]\\s*=\\s*[\"'][Ss]erver")
 (#set! injection.language "c_sharp"))

((script_element
   (start_tag) @_tag
   (vb_code) @injection.content)
 (#match? @_tag "[Rr][Uu][Nn][Aa][Tt]\\s*=\\s*[\"'][Ss]erver")
 (#set! injection.language "vb"))

((script_element
   (start_tag) @_tag
   [(c_sharp_code) (vb_code)] @injection.content)
 (#not-match? @_tag "[Rr][Uu][Nn][Aa][Tt]\\s*=\\s*[\"'][Ss]erver")
 (#set! injection.language "javascript"))

((style_element
   (style_text) @injection.content)
 (#set! injection.language "css"))
