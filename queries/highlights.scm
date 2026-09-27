; Syntax highlighting for ASP.NET Web Forms markup.

(directive_name) @keyword
(tag_name) @tag
(attribute_name) @attribute
(attribute_value) @string
(language_value) @type
(doctype_token) @keyword
(doctype_declaration) @string

(server_comment) @comment
(server_comment_text) @comment
(html_comment) @comment
(comment_text) @comment

(text) @text
(style_text) @string
(expression_value) @string

["<%" "<%=" "<%:" "<%#" "<%$" "%>" "<%@" "<%--" "--%>"] @punctuation.special
["<" ">" "</" "/>"] @punctuation.bracket
["="] @operator
