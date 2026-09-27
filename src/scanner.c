// External scanner for tree-sitter-aspx.
//
// Three jobs the ordinary lexer cannot do:
//
//   * Code, comments, style, and handler bodies run until a terminator
//     (`%>`, `--%>`, `</script>`, `</style>`, or EOF). Tree-sitter's
//     regex dialect has no lookahead, so a regex cannot stop before that
//     terminator without eating it.
//   * The page `Language` value is recorded here. Later code tokens are
//     emitted as C# or VB so the injection query can name the grammar
//     without finding the directive again. The byte is serialized because
//     an incremental reparse may resume after the directive.
//   * A `Language` value that is neither C# nor VB is still consumed, and
//     the recorded language stays unchanged.
#include "tree_sitter/parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

enum TokenType {
  LANG_CSHARP,
  LANG_VB,
  LANG_OTHER,
  CODE_CSHARP,
  CODE_VB,
  SERVER_COMMENT_TEXT,
  SCRIPT_CSHARP,
  SCRIPT_VB,
  STYLE_TEXT,
  EXPRESSION_TEXT,
  HANDLER_TEXT,
};

enum Language {
  LANGUAGE_CSHARP = 0,
  LANGUAGE_VB = 1,
};

typedef struct {
  uint8_t language;
} Scanner;

static bool is_space(int32_t c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

void *tree_sitter_aspx_external_scanner_create(void) {
  Scanner *scanner = calloc(1, sizeof(Scanner));
  scanner->language = LANGUAGE_CSHARP;
  return scanner;
}

void tree_sitter_aspx_external_scanner_destroy(void *payload) {
  free(payload);
}

unsigned tree_sitter_aspx_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = payload;
  buffer[0] = (char)scanner->language;
  return 1;
}

void tree_sitter_aspx_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = payload;
  scanner->language = length > 0 ? (uint8_t)buffer[0] : LANGUAGE_CSHARP;
}

static bool scan_until_percent_gt(TSLexer *lexer) {
  bool has_content = false;
  while (lexer->lookahead != 0) {
    if (lexer->lookahead == '%') {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);
      if (lexer->lookahead == '>') {
        return has_content;
      }
      lexer->mark_end(lexer);
      has_content = true;
      continue;
    }
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    has_content = true;
  }
  return has_content;
}

static bool scan_until_server_comment_end(TSLexer *lexer) {
  bool has_content = false;
  while (lexer->lookahead != 0) {
    if (lexer->lookahead == '-') {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);
      if (lexer->lookahead == '-') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '%') {
          lexer->advance(lexer, false);
          if (lexer->lookahead == '>') {
            return has_content;
          }
        }
      }
      lexer->mark_end(lexer);
      has_content = true;
      continue;
    }
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    has_content = true;
  }
  return has_content;
}

static bool match_ci(TSLexer *lexer, const char *lower) {
  for (unsigned i = 0; lower[i] != '\0'; i++) {
    int32_t c = lexer->lookahead;
    if (c == 0 || towlower(c) != (wint_t)lower[i]) {
      return false;
    }
    lexer->advance(lexer, false);
  }
  return true;
}

// `</name` with optional whitespace before `>`. On a match the token end
// stays where the caller marked it, before the `<`. Otherwise the consumed
// characters become content.
static bool scan_until_end_tag(TSLexer *lexer, const char *name) {
  bool has_content = false;
  while (lexer->lookahead != 0) {
    if (lexer->lookahead == '<') {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);
      if (lexer->lookahead == '/') {
        lexer->advance(lexer, false);
        if (match_ci(lexer, name)) {
          while (is_space(lexer->lookahead)) {
            lexer->advance(lexer, false);
          }
          if (lexer->lookahead == '>') {
            return has_content;
          }
        }
      }
      lexer->mark_end(lexer);
      has_content = true;
      continue;
    }
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    has_content = true;
  }
  return has_content;
}

static bool scan_language(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  char buf[32];
  unsigned n = 0;
  while (n + 1 < sizeof(buf)) {
    int32_t c = lexer->lookahead;
    if (c == 0 || c == '"' || c == '\'' || is_space(c) || c > 127) {
      break;
    }
    buf[n++] = (char)tolower((unsigned char)c);
    lexer->advance(lexer, false);
  }
  buf[n] = '\0';
  if (n == 0) {
    return false;
  }
  lexer->mark_end(lexer);

  enum TokenType symbol = LANG_OTHER;
  if (strcmp(buf, "c#") == 0 || strcmp(buf, "csharp") == 0) {
    symbol = LANG_CSHARP;
    scanner->language = LANGUAGE_CSHARP;
  } else if (strcmp(buf, "vb") == 0 || strcmp(buf, "visualbasic") == 0) {
    symbol = LANG_VB;
    scanner->language = LANGUAGE_VB;
  }
  if (!valid_symbols[symbol]) {
    return false;
  }
  lexer->result_symbol = symbol;
  return true;
}

static bool emit_code(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols, enum TokenType csharp, enum TokenType vb) {
  enum TokenType symbol = scanner->language == LANGUAGE_VB ? vb : csharp;
  if (!valid_symbols[symbol]) {
    return false;
  }
  bool is_script = csharp == SCRIPT_CSHARP;
  if (is_script) {
    if (!scan_until_end_tag(lexer, "script")) {
      return false;
    }
  } else if (!scan_until_percent_gt(lexer)) {
    return false;
  }
  lexer->result_symbol = symbol;
  return true;
}

bool tree_sitter_aspx_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = payload;

  if (valid_symbols[LANG_CSHARP] || valid_symbols[LANG_VB] || valid_symbols[LANG_OTHER]) {
    return scan_language(lexer, scanner, valid_symbols);
  }

  if (valid_symbols[SERVER_COMMENT_TEXT]) {
    if (!scan_until_server_comment_end(lexer)) {
      return false;
    }
    lexer->result_symbol = SERVER_COMMENT_TEXT;
    return true;
  }

  if (valid_symbols[CODE_CSHARP] || valid_symbols[CODE_VB]) {
    return emit_code(lexer, scanner, valid_symbols, CODE_CSHARP, CODE_VB);
  }

  if (valid_symbols[SCRIPT_CSHARP] || valid_symbols[SCRIPT_VB]) {
    return emit_code(lexer, scanner, valid_symbols, SCRIPT_CSHARP, SCRIPT_VB);
  }

  if (valid_symbols[STYLE_TEXT]) {
    if (!scan_until_end_tag(lexer, "style")) {
      return false;
    }
    lexer->result_symbol = STYLE_TEXT;
    return true;
  }

  if (valid_symbols[EXPRESSION_TEXT]) {
    if (!scan_until_percent_gt(lexer)) {
      return false;
    }
    lexer->result_symbol = EXPRESSION_TEXT;
    return true;
  }

  if (valid_symbols[HANDLER_TEXT]) {
    if (lexer->lookahead == 0) {
      return false;
    }
    while (lexer->lookahead != 0) {
      lexer->advance(lexer, false);
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = HANDLER_TEXT;
    return true;
  }

  return false;
}
