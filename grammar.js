/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// Lexical precedence above the general tag/attribute names, so `script`,
// `br`, and `Language` win when they are the same length as the general
// pattern. Longer names (`scriptmanager`, `break`) still take the general
// pattern because the lexer prefers the longest match.
const RESERVED = 1;

/**
 * @param {string} word
 */
function reserved(word) {
  return token(
    prec(
      RESERVED,
      new RegExp(
        word
          .split('')
          .map((ch) => {
            const lower = ch.toLowerCase();
            const upper = ch.toUpperCase();
            return lower === upper
              ? ch.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
              : `[${upper}${lower}]`;
          })
          .join(''),
      ),
    ),
  );
}

const VOID_TAGS = [
  'area',
  'base',
  'br',
  'col',
  'embed',
  'hr',
  'img',
  'input',
  'link',
  'meta',
  'param',
  'source',
  'track',
  'wbr',
];

export default grammar({
  name: 'aspx',

  // A UTF-8 BOM is whitespace as far as the page parser is concerned. UTF-16
  // files are decoded to UTF-8 before parsing; see tools/corpus-check.mjs.
  extras: (_) => [/\s/, /\uFEFF/],

  // Reserved names (`col`, `script`, `Language`, `WebHandler`) must not match
  // a prefix of a longer name (`Columns`, `asp:ScriptManager`). Keywords are
  // recognized only against this word token, so directive names and tag names
  // both alias it.
  word: ($) => $._name,

  externals: ($) => [
    $._lang_csharp,
    $._lang_vb,
    $._lang_other,
    $._code_csharp,
    $._code_vb,
    $.server_comment_text,
    $._script_csharp,
    $._script_vb,
    $.style_text,
    $.expression_text,
    $.handler_text,
  ],

  rules: {
    // WebHandler / WebService files are a directive plus code, not markup.
    // The reserved directive name is what selects that alternative. An empty
    // file is the start rule matching nothing; `document` itself cannot be
    // empty because tree-sitter rejects a non-start rule that matches "".
    source_file: ($) => optional(choice($.handler_file, $.document)),

    document: ($) => repeat1($._top),

    handler_file: ($) =>
      seq(
        $.handler_directive,
        repeat(choice($.server_comment, $.script_element, alias($.handler_text, $.handler_code))),
      ),

    _top: ($) => choice($._content, $.end_tag),

    _content: ($) =>
      choice(
        $.directive,
        $.server_comment,
        $.html_comment,
        $._nugget,
        $.script_element,
        $.style_element,
        $.void_element,
        $.self_closing_element,
        $.element,
        $.doctype,
        $.text,
      ),

    directive: ($) => seq('<%@', field('name', $.directive_name), repeat($._attribute), '%>'),

    handler_directive: ($) =>
      seq(
        '<%@',
        field('name', alias(choice($.web_handler_name, $.web_service_name), $.directive_name)),
        repeat($._attribute),
        '%>',
      ),

    _name: (_) => /[A-Za-z_][A-Za-z0-9_.:-]*/,
    directive_name: ($) => $._name,
    web_handler_name: (_) => choice('WebHandler', 'webhandler', 'WEBHANDLER'),
    web_service_name: (_) => choice('WebService', 'webservice', 'WEBSERVICE'),

    // `Language` is its own attribute so the value is a language_value node
    // and the scanner can record C# vs VB for later code tokens.
    language_attribute: ($) =>
      seq(
        field('name', alias($.language_name, $.attribute_name)),
        '=',
        choice(
          seq('"', field('value', $._language_value), '"'),
          seq("'", field('value', $._language_value), "'"),
        ),
      ),

    language_name: (_) => reserved('Language'),

    _language_value: ($) =>
      alias(choice($._lang_csharp, $._lang_vb, $._lang_other), $.language_value),

    _attribute: ($) => choice($.language_attribute, $.attribute),

    attribute: ($) =>
      seq(
        field('name', $.attribute_name),
        optional(
          seq(
            '=',
            field(
              'value',
              choice(
                $.quoted_attribute_value,
                alias($.unquoted_attribute_value, $.attribute_value),
              ),
            ),
          ),
        ),
      ),

    attribute_name: (_) => /[A-Za-z_:][A-Za-z0-9_.:-]*/,

    quoted_attribute_value: ($) =>
      choice(
        seq('"', repeat($._double_quoted_chunk), '"'),
        seq("'", repeat($._single_quoted_chunk), "'"),
      ),

    _double_quoted_chunk: ($) =>
      choice(
        $._nugget,
        $.server_comment,
        alias(/[^"<%]+/, $.attribute_value),
        alias('%', $.attribute_value),
        alias('<', $.attribute_value),
      ),

    _single_quoted_chunk: ($) =>
      choice(
        $._nugget,
        $.server_comment,
        alias(/[^'<%]+/, $.attribute_value),
        alias('%', $.attribute_value),
        alias('<', $.attribute_value),
      ),

    unquoted_attribute_value: (_) => /[^"'=<>\s`]+/,

    server_comment: ($) => seq('<%--', optional($.server_comment_text), '--%>'),

    // Nuggets inside an HTML comment still execute, so they stay real nodes.
    html_comment: ($) =>
      seq(
        '<!--',
        repeat(
          choice(
            $._nugget,
            $.server_comment,
            $.directive,
            alias(/[^-<>]+/, $.comment_text),
            alias('-', $.comment_text),
            alias('<', $.comment_text),
            alias('>', $.comment_text),
          ),
        ),
        '-->',
      ),

    _nugget: ($) =>
      choice(
        $.encoded_expression,
        $.output_expression,
        $.binding_expression,
        $.expression_builder,
        $.statement_block,
      ),

    _embedded_code: ($) =>
      choice(alias($._code_csharp, $.c_sharp_code), alias($._code_vb, $.vb_code)),

    encoded_expression: ($) => seq('<%:', optional($._embedded_code), '%>'),
    output_expression: ($) => seq('<%=', optional($._embedded_code), '%>'),
    binding_expression: ($) => seq('<%#', optional($._embedded_code), '%>'),
    statement_block: ($) => seq('<%', optional($._embedded_code), '%>'),

    // Expression builders (`<%$ AppSettings:Key %>`) are not C# or VB.
    expression_builder: ($) =>
      seq('<%$', optional(alias($.expression_text, $.expression_value)), '%>'),

    script_element: ($) =>
      seq(
        alias($.script_start_tag, $.start_tag),
        optional(choice(alias($._script_csharp, $.c_sharp_code), alias($._script_vb, $.vb_code))),
        alias($.script_end_tag, $.end_tag),
      ),

    script_start_tag: ($) =>
      seq('<', field('name', alias($.script_tag_name, $.tag_name)), repeat($._attribute), '>'),

    script_end_tag: ($) => seq('</', field('name', alias($.script_tag_name, $.tag_name)), '>'),

    script_tag_name: (_) => reserved('script'),

    style_element: ($) =>
      seq(
        alias($.style_start_tag, $.start_tag),
        optional($.style_text),
        alias($.style_end_tag, $.end_tag),
      ),

    style_start_tag: ($) =>
      seq('<', field('name', alias($.style_tag_name, $.tag_name)), repeat($._attribute), '>'),

    style_end_tag: ($) => seq('</', field('name', alias($.style_tag_name, $.tag_name)), '>'),

    style_tag_name: (_) => reserved('style'),

    void_element: ($) =>
      seq(
        '<',
        field('name', alias($.void_tag_name, $.tag_name)),
        repeat($._attribute),
        choice('/>', '>'),
      ),

    void_tag_name: (_) => choice(...VOID_TAGS.map((name) => reserved(name))),

    self_closing_element: ($) => seq('<', field('name', $.tag_name), repeat($._attribute), '/>'),

    element: ($) => seq($.start_tag, repeat($._content), $.end_tag),

    start_tag: ($) => seq('<', field('name', $.tag_name), repeat($._attribute), '>'),

    end_tag: ($) =>
      seq(
        '</',
        field(
          'name',
          choice(
            alias($.script_tag_name, $.tag_name),
            alias($.style_tag_name, $.tag_name),
            alias($.void_tag_name, $.tag_name),
            $.tag_name,
          ),
        ),
        '>',
      ),

    tag_name: ($) => $._name,

    doctype: ($) => seq($.doctype_token, optional(alias(/[^>]+/, $.doctype_declaration)), '>'),

    doctype_token: (_) => token(prec(RESERVED, /<![Dd][Oo][Cc][Tt][Yy][Pp][Ee]/)),

    text: (_) => /[^<]+/,
  },
});
