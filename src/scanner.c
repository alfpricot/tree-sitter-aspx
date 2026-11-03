// External scanner for tree-sitter-aspx.
//
// Three jobs the ordinary lexer cannot do:
//
//   * Code, comments, style, and handler bodies run until a terminator
//     (`%>`, `--%>`, `</script>`, `</style>`, or EOF). Script and style
//     bodies also stop at `<%`, so nuggets inside them stay real nodes. Tree-sitter's
//     regex dialect has no lookahead, so a regex cannot stop before that
//     terminator without eating it.
//   * The page `Language` value is recorded here. Later code tokens are
//     emitted as C# or VB so the injection query can name the grammar
//     without finding the directive again. The byte is serialized because
//     an incremental reparse may resume after the directive.
//   * A `Language` value that is neither C# nor VB is still consumed, and
//     the recorded language stays unchanged.
#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#include <string.h>

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

// ASCII-only lowercase. The Wasm parser target provides no libc ctype
// functions, and every name compared here is ASCII by construction.
static int32_t ascii_lower(int32_t c) {
  return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static bool is_space(int32_t c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

void *tree_sitter_aspx_external_scanner_create(void) {
  // ts_malloc plus an explicit field set, not ts_calloc: the compiler can
  // lower calloc to memset, which the Wasm parser target does not provide.
  // ts_malloc is plain malloc unless the host build opts into tree-sitter's
  // allocator, and the Wasm target has no abort(), so a failed allocation
  // returns NULL. The other entry points then do nothing: the parse goes on
  // without external tokens instead of writing through a null pointer.
  Scanner *scanner = ts_malloc(sizeof(Scanner));
  if (scanner != NULL) {
    scanner->language = LANGUAGE_CSHARP;
  }
  return scanner;
}

void tree_sitter_aspx_external_scanner_destroy(void *payload) {
  ts_free(payload);
}

unsigned tree_sitter_aspx_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = payload;
  if (scanner == NULL) {
    return 0;
  }
  buffer[0] = (char)scanner->language;
  return 1;
}

void tree_sitter_aspx_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = payload;
  if (scanner == NULL) {
    return;
  }
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

// Content runs until a run of two or more dashes followed by `%>`. The
// whole run belongs to the closing token, so `---%>` closes the comment.
static bool scan_until_server_comment_end(TSLexer *lexer) {
  bool has_content = false;
  while (lexer->lookahead != 0) {
    if (lexer->lookahead == '-') {
      lexer->mark_end(lexer);
      unsigned dashes = 0;
      while (lexer->lookahead == '-') {
        lexer->advance(lexer, false);
        dashes++;
      }
      if (dashes >= 2 && lexer->lookahead == '%') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '>') {
          return has_content;
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
    if (c == 0 || ascii_lower(c) != lower[i]) {
      return false;
    }
    lexer->advance(lexer, false);
  }
  return true;
}

// Content runs until `</name` with optional whitespace before `>`, or until
// a `<%` that opens a nugget or server comment inside the body. On a stop the
// token end stays where the caller marked it, before the `<`. Otherwise the
// consumed characters become content.
static bool scan_until_end_tag(TSLexer *lexer, const char *name) {
  bool has_content = false;
  while (lexer->lookahead != 0) {
    if (lexer->lookahead == '<') {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);
      if (lexer->lookahead == '%') {
        return has_content;
      }
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

// The CodeDom names ASP.NET accepts for each compiler. Matching ignores
// case; anything else is LANG_OTHER and leaves the page language alone.
static const char *const CSHARP_NAMES[] = {"c#", "cs", "csharp"};
static const char *const VB_NAMES[] = {"vb", "vbs", "visualbasic", "vbscript"};

static bool name_in(const char *name, const char *const *names, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    if (strcmp(name, names[i]) == 0) {
      return true;
    }
  }
  return false;
}

// A quoted value stops at the quote; an unquoted one also stops at the `>`
// that ends the tag. scan_language handles `%>` itself.
static bool ends_language_value(int32_t c) {
  return c == 0 || c == '"' || c == '\'' || c == '>' || is_space(c) || c > 127;
}

static bool scan_language(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  while (is_space(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }
  // Names longer than the buffer are still consumed whole; none of them is
  // C# or VB, so they become LANG_OTHER.
  char buf[32];
  unsigned n = 0;
  bool truncated = false;
  for (;;) {
    int32_t c = lexer->lookahead;
    // A `%` ends an unquoted value only when it starts the closing `%>`.
    if (c == '%') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == '>') {
        break;
      }
    } else if (ends_language_value(c)) {
      break;
    } else {
      lexer->advance(lexer, false);
    }
    if (n + 1 < sizeof(buf)) {
      buf[n++] = (char)ascii_lower(c);
    } else {
      truncated = true;
    }
    lexer->mark_end(lexer);
  }
  buf[n] = '\0';
  if (n == 0) {
    return false;
  }

  enum TokenType symbol = LANG_OTHER;
  if (truncated) {
    // Too long to be any recognized name.
  } else if (name_in(buf, CSHARP_NAMES, sizeof(CSHARP_NAMES) / sizeof(CSHARP_NAMES[0]))) {
    symbol = LANG_CSHARP;
  } else if (name_in(buf, VB_NAMES, sizeof(VB_NAMES) / sizeof(VB_NAMES[0]))) {
    symbol = LANG_VB;
  }
  if (!valid_symbols[symbol]) {
    return false;
  }
  if (symbol == LANG_CSHARP) {
    scanner->language = LANGUAGE_CSHARP;
  } else if (symbol == LANG_VB) {
    scanner->language = LANGUAGE_VB;
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
  if (scanner == NULL) {
    return false;
  }

  // During error recovery every external token is valid at once. No real
  // parse state allows both a Language value and handler text, so that pair
  // identifies recovery. Scanning then would turn arbitrary text into a
  // language value and could switch the language for the rest of the page.
  if (valid_symbols[LANG_CSHARP] && valid_symbols[HANDLER_TEXT]) {
    return false;
  }

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
