// External scanner for tree-sitter-aspx.
//
// Jobs the ordinary lexer cannot do:
//
//   * Code, comments, style, and handler bodies run until a terminator
//     (`%>`, `--%>`, `</script>`, `</style>`, or EOF). Script and style
//     bodies also stop at `<%`, so nuggets inside them stay real nodes.
//     Tree-sitter's regex dialect has no lookahead, so a regex cannot stop
//     before that terminator without eating it.
//   * The page `Language` value is recorded here. Later code tokens are
//     emitted as C# or VB so the injection query can name the grammar
//     without finding the directive again. The byte is serialized because
//     an incremental reparse may resume after the directive.
//   * A `Language` value that is neither C# nor VB is still consumed, and
//     the recorded language stays unchanged.
//   * Start and end tag names pass through here so the scanner can keep a
//     stack of open elements and emit a zero-width implied end tag where
//     HTML lets a page leave one out. The stack is serialized with the
//     language.
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
  HANDLER_CSHARP,
  HANDLER_VB,
  START_TAG_NAME,
  END_TAG_NAME,
  IMPLICIT_END_TAG,
  SELF_CLOSING_DELIMITER,
};

enum Language {
  LANGUAGE_CSHARP = 0,
  LANGUAGE_VB = 1,
};

// Open elements, innermost last, as lowercase names. Names longer than
// TAG_NAME_MAX are cut to that length; elements nested deeper than
// TAG_STACK_MAX are counted but not named, and never match an end tag.
#define TAG_STACK_MAX 64
#define TAG_NAME_MAX 31

typedef struct {
  uint8_t language;
  uint16_t depth;
  char names[TAG_STACK_MAX][TAG_NAME_MAX + 1];
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
    scanner->depth = 0;
  }
  return scanner;
}

void tree_sitter_aspx_external_scanner_destroy(void *payload) {
  ts_free(payload);
}

// Layout: language, depth (two bytes, little-endian), the number of names
// stored, then each stored name as a length byte and its characters,
// outermost first. Names that do not fit are restored as unknown.
unsigned tree_sitter_aspx_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = payload;
  if (scanner == NULL) {
    return 0;
  }
  buffer[0] = (char)scanner->language;
  buffer[1] = (char)(scanner->depth & 0xff);
  buffer[2] = (char)(scanner->depth >> 8);
  unsigned size = 4;
  unsigned stored = 0;
  unsigned named = scanner->depth < TAG_STACK_MAX ? scanner->depth : TAG_STACK_MAX;
  while (stored < named && stored < 255) {
    const char *name = scanner->names[stored];
    unsigned length = (unsigned)strlen(name);
    if (size + 1 + length > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) {
      break;
    }
    buffer[size++] = (char)length;
    for (unsigned i = 0; i < length; i++) {
      buffer[size++] = name[i];
    }
    stored++;
  }
  buffer[3] = (char)stored;
  return size;
}

void tree_sitter_aspx_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = payload;
  if (scanner == NULL) {
    return;
  }
  scanner->language = LANGUAGE_CSHARP;
  scanner->depth = 0;
  if (length == 0) {
    return;
  }
  scanner->language = (uint8_t)buffer[0];
  if (length < 4) {
    return;
  }
  scanner->depth = (uint16_t)((uint8_t)buffer[1] | ((uint8_t)buffer[2] << 8));
  unsigned stored = (uint8_t)buffer[3];
  unsigned named = scanner->depth < TAG_STACK_MAX ? scanner->depth : TAG_STACK_MAX;
  unsigned offset = 4;
  for (unsigned i = 0; i < named; i++) {
    unsigned n = 0;
    if (i < stored && offset < length) {
      unsigned name_length = (uint8_t)buffer[offset++];
      while (n < name_length && n < TAG_NAME_MAX && offset < length) {
        scanner->names[i][n++] = buffer[offset++];
      }
      offset += name_length - n;
    }
    scanner->names[i][n] = '\0';
  }
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

// After `</`, matches `name` with optional whitespace before `>`.
static bool at_end_tag_name(TSLexer *lexer, const char *name) {
  if (!match_ci(lexer, name)) {
    return false;
  }
  while (is_space(lexer->lookahead)) {
    lexer->advance(lexer, false);
  }
  return lexer->lookahead == '>';
}

// Called with the lexer just past the `<` of a `<%` at the start of a body
// chunk. Reports whether a `%>` closes it before the body's end tag. When it
// does not, the `<%` is literal text (for example inside a string in a server
// script), so the token end is moved to the end tag or EOF as the scan goes.
static bool nugget_closes_in_body(TSLexer *lexer, const char *name) {
  lexer->advance(lexer, false);
  // The `<%` itself is content when nothing closes it, even at the end of
  // the file; a zero-width chunk would repeat forever.
  lexer->mark_end(lexer);
  while (lexer->lookahead != 0) {
    if (lexer->lookahead == '%') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == '>') {
        return true;
      }
      lexer->mark_end(lexer);
      continue;
    }
    if (lexer->lookahead == '<') {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);
      if (lexer->lookahead == '/') {
        lexer->advance(lexer, false);
        if (at_end_tag_name(lexer, name)) {
          return false;
        }
      }
      lexer->mark_end(lexer);
      continue;
    }
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
  }
  return false;
}

// Content runs until `</name` with optional whitespace before `>`, or until
// a `<%` that opens a nugget or server comment inside the body. On a stop the
// token end stays where the caller marked it, before the `<`. A `<%` with no
// `%>` before the end tag is not a nugget and stays in the content.
static bool scan_until_end_tag(TSLexer *lexer, const char *name) {
  bool has_content = false;
  while (lexer->lookahead != 0) {
    if (lexer->lookahead == '<') {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);
      if (lexer->lookahead == '%') {
        // Stop before the `<%`; the next scan starts there and decides
        // whether it opens a nugget.
        if (has_content) {
          return true;
        }
        return !nugget_closes_in_body(lexer, name);
      }
      if (lexer->lookahead == '/') {
        lexer->advance(lexer, false);
        if (at_end_tag_name(lexer, name)) {
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

// Case-insensitive search for `word` ahead of the lexer, stopping at `stop`
// (or EOF). Consumes what it reads.
static bool find_ci_before(TSLexer *lexer, const char *word, int32_t stop) {
  while (lexer->lookahead != 0 && lexer->lookahead != stop) {
    if (ascii_lower(lexer->lookahead) == word[0]) {
      lexer->advance(lexer, false);
      if (match_ci(lexer, word + 1)) {
        return true;
      }
      continue;
    }
    lexer->advance(lexer, false);
  }
  return false;
}

// Consumes up to and including `--%>`, reporting whether it was found.
static bool find_comment_end(TSLexer *lexer) {
  unsigned dashes = 0;
  while (lexer->lookahead != 0) {
    int32_t c = lexer->lookahead;
    lexer->advance(lexer, false);
    if (c == '-') {
      dashes++;
    } else if (c == '%' && dashes >= 2 && lexer->lookahead == '>') {
      return true;
    } else {
      dashes = 0;
    }
  }
  return false;
}

// With the lexer just past a line-start `<`, reports whether what follows is
// markup the handler grammar can finish, so the code should stop before it:
//   * `<%@ ... %>`, a directive such as `<%@ Assembly Name="..." %>`;
//   * `<%-- ... --%>`, a complete server comment;
//   * `<script ... runat ... >` that is not self-closing and has a later
//     `</script>`. Only server scripts mean anything in a handler file; a
//     client `<script>` line is almost always HTML inside a C# or VB string.
// Consumes what it reads; on false the caller treats all of it as code.
static bool at_handler_markup(TSLexer *lexer) {
  if (lexer->lookahead == '%') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '@') {
      return find_ci_before(lexer, "%>", 0);
    }
    if (lexer->lookahead != '-') {
      return false;
    }
    lexer->advance(lexer, false);
    if (lexer->lookahead != '-') {
      return false;
    }
    lexer->advance(lexer, false);
    return find_comment_end(lexer);
  }
  if (!match_ci(lexer, "script") ||
      !(is_space(lexer->lookahead) || lexer->lookahead == '>')) {
    return false;
  }
  bool runat = false;
  int32_t previous = 0;
  while (lexer->lookahead != 0 && lexer->lookahead != '>') {
    if (ascii_lower(lexer->lookahead) == 'r') {
      lexer->advance(lexer, false);
      if (match_ci(lexer, "unat")) {
        runat = true;
      }
      previous = 0;
      continue;
    }
    previous = lexer->lookahead;
    lexer->advance(lexer, false);
  }
  if (!runat || previous == '/' || lexer->lookahead != '>') {
    return false;
  }
  return find_ci_before(lexer, "</script", 0);
}

// The body of a WebHandler or WebService file is code up to EOF, except that
// a line starting (after optional indentation) with a directive, a server
// comment, or a server script begins that construct instead. Leading
// whitespace is skipped, so the code starts at its first non-space character.
static bool scan_handler_code(TSLexer *lexer) {
  while (is_space(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }
  bool has_content = false;
  bool line_start = true;
  while (lexer->lookahead != 0) {
    int32_t c = lexer->lookahead;
    if (c == '<' && line_start) {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);
      if (at_handler_markup(lexer)) {
        return has_content;
      }
      // Not markup: everything just read is code. It may have crossed
      // newlines, so the next `<` is not treated as a line start.
      lexer->mark_end(lexer);
      has_content = true;
      line_start = false;
      continue;
    }
    if (c == '\n') {
      line_start = true;
    } else if (c != ' ' && c != '\t' && c != '\r') {
      line_start = false;
    }
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    has_content = true;
  }
  return has_content;
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

// ---- Open-element stack -------------------------------------------------

static bool is_name_start(int32_t c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool is_name_char(int32_t c) {
  return is_name_start(c) || (c >= '0' && c <= '9') || c == '.' || c == ':' || c == '-';
}

// Reads a tag name (the grammar's `_name` pattern) as lowercase into `out`,
// keeping the first TAG_NAME_MAX characters. Returns false if none is there.
static bool read_tag_name(TSLexer *lexer, char *out) {
  if (!is_name_start(lexer->lookahead)) {
    return false;
  }
  unsigned n = 0;
  while (is_name_char(lexer->lookahead)) {
    if (n < TAG_NAME_MAX) {
      out[n++] = (char)ascii_lower(lexer->lookahead);
    }
    lexer->advance(lexer, false);
  }
  out[n] = '\0';
  return true;
}

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

// Names the grammar lexes itself as reserved words: void elements never hold
// content, and script and style bodies are raw text with their own end tags.
static const char *const RESERVED_TAGS[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", "script", "style",
};

// Start tags that close an open `p`, per the HTML parsing rules.
static const char *const CLOSES_P[] = {
    "address", "article", "aside", "blockquote", "details", "dialog", "div",
    "dl", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2",
    "h3", "h4", "h5", "h6", "header", "hgroup", "hr", "main", "menu", "nav",
    "ol", "p", "pre", "section", "table", "ul",
};

static const char *const DT_DD[] = {"dt", "dd"};
static const char *const RT_RP[] = {"rt", "rp"};
static const char *const OPTION_CLOSERS[] = {"option", "optgroup"};
static const char *const ROW_CLOSERS[] = {"tr", "tbody", "tfoot", "thead"};
static const char *const CELL_CLOSERS[] = {"td", "th", "tr", "tbody", "tfoot", "thead"};
static const char *const SECTION_CLOSERS[] = {"tbody", "tfoot"};

// Whether a start tag named `next` implies the end of the open element `open`.
static bool start_tag_closes(const char *open, const char *next) {
  if (strcmp(open, "p") == 0) {
    return name_in(next, CLOSES_P, COUNT(CLOSES_P));
  }
  if (strcmp(open, "li") == 0) {
    return strcmp(next, "li") == 0;
  }
  if (strcmp(open, "dt") == 0 || strcmp(open, "dd") == 0) {
    return name_in(next, DT_DD, COUNT(DT_DD));
  }
  if (strcmp(open, "rt") == 0 || strcmp(open, "rp") == 0) {
    return name_in(next, RT_RP, COUNT(RT_RP));
  }
  if (strcmp(open, "option") == 0) {
    return name_in(next, OPTION_CLOSERS, COUNT(OPTION_CLOSERS));
  }
  if (strcmp(open, "optgroup") == 0) {
    return strcmp(next, "optgroup") == 0;
  }
  if (strcmp(open, "tr") == 0) {
    return name_in(next, ROW_CLOSERS, COUNT(ROW_CLOSERS));
  }
  if (strcmp(open, "td") == 0 || strcmp(open, "th") == 0) {
    return name_in(next, CELL_CLOSERS, COUNT(CELL_CLOSERS));
  }
  if (strcmp(open, "thead") == 0 || strcmp(open, "tbody") == 0) {
    return name_in(next, SECTION_CLOSERS, COUNT(SECTION_CLOSERS));
  }
  return false;
}

static const char *top_name(Scanner *scanner) {
  if (scanner->depth == 0 || scanner->depth > TAG_STACK_MAX) {
    return "";
  }
  return scanner->names[scanner->depth - 1];
}

static void push_tag(Scanner *scanner, const char *name) {
  if (scanner->depth < TAG_STACK_MAX) {
    char *slot = scanner->names[scanner->depth];
    unsigned i = 0;
    for (; name[i] != '\0'; i++) {
      slot[i] = name[i];
    }
    slot[i] = '\0';
  }
  if (scanner->depth < UINT16_MAX) {
    scanner->depth++;
  }
}

static void pop_tag(Scanner *scanner) {
  if (scanner->depth > 0) {
    scanner->depth--;
  }
}

// Whether `name` is an open element below the innermost one.
static bool open_below_top(Scanner *scanner, const char *name) {
  unsigned named = scanner->depth < TAG_STACK_MAX ? scanner->depth : TAG_STACK_MAX;
  for (unsigned i = 0; i + 1 < named; i++) {
    if (strcmp(scanner->names[i], name) == 0) {
      return true;
    }
  }
  return false;
}

// A zero-width token that ends the innermost open element before the next
// start tag, end tag, or the end of the file, when HTML implies that end.
static bool scan_implicit_end_tag(TSLexer *lexer, Scanner *scanner) {
  while (is_space(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }
  lexer->mark_end(lexer);
  if (scanner->depth == 0) {
    return false;
  }
  bool implied = false;
  if (lexer->eof(lexer)) {
    implied = true;
  } else if (lexer->lookahead == '<') {
    lexer->advance(lexer, false);
    char name[TAG_NAME_MAX + 1];
    if (lexer->lookahead == '/') {
      lexer->advance(lexer, false);
      // An end tag for an element further out closes this one first. An end
      // tag that names no open element still closes the innermost one, as
      // before, so it is not implied here.
      implied = read_tag_name(lexer, name) && strcmp(name, top_name(scanner)) != 0 &&
                open_below_top(scanner, name);
    } else if (read_tag_name(lexer, name)) {
      implied = start_tag_closes(top_name(scanner), name);
    }
  }
  if (!implied) {
    return false;
  }
  pop_tag(scanner);
  lexer->result_symbol = IMPLICIT_END_TAG;
  return true;
}

static bool scan_start_tag_name(TSLexer *lexer, Scanner *scanner) {
  char name[TAG_NAME_MAX + 1];
  if (!read_tag_name(lexer, name) || name_in(name, RESERVED_TAGS, COUNT(RESERVED_TAGS))) {
    return false;
  }
  lexer->mark_end(lexer);
  push_tag(scanner, name);
  lexer->result_symbol = START_TAG_NAME;
  return true;
}

// An end tag closes the innermost open element whether or not the names
// match; implied end tags have already closed anything between it and a
// matching element further out.
static bool scan_end_tag_name(TSLexer *lexer, Scanner *scanner) {
  while (is_space(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }
  char name[TAG_NAME_MAX + 1];
  if (!read_tag_name(lexer, name)) {
    return false;
  }
  lexer->mark_end(lexer);
  pop_tag(scanner);
  lexer->result_symbol = END_TAG_NAME;
  return true;
}

static bool scan_self_closing_delimiter(TSLexer *lexer, Scanner *scanner) {
  while (is_space(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }
  if (lexer->lookahead != '/') {
    return false;
  }
  lexer->advance(lexer, false);
  if (lexer->lookahead != '>') {
    return false;
  }
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);
  pop_tag(scanner);
  lexer->result_symbol = SELF_CLOSING_DELIMITER;
  return true;
}

bool tree_sitter_aspx_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = payload;
  if (scanner == NULL) {
    return false;
  }

  // During error recovery every external token is valid at once. No real
  // parse state allows both a Language value and handler code, so that pair
  // identifies recovery. Scanning then would turn arbitrary text into a
  // language value and could switch the language for the rest of the page.
  if (valid_symbols[LANG_CSHARP] && valid_symbols[HANDLER_CSHARP]) {
    return false;
  }

  if (valid_symbols[IMPLICIT_END_TAG]) {
    return scan_implicit_end_tag(lexer, scanner);
  }

  if (valid_symbols[START_TAG_NAME]) {
    return scan_start_tag_name(lexer, scanner);
  }

  if (valid_symbols[END_TAG_NAME]) {
    return scan_end_tag_name(lexer, scanner);
  }

  if (valid_symbols[SELF_CLOSING_DELIMITER]) {
    return scan_self_closing_delimiter(lexer, scanner);
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

  if (valid_symbols[HANDLER_CSHARP] || valid_symbols[HANDLER_VB]) {
    enum TokenType symbol = scanner->language == LANGUAGE_VB ? HANDLER_VB : HANDLER_CSHARP;
    if (!valid_symbols[symbol] || !scan_handler_code(lexer)) {
      return false;
    }
    lexer->result_symbol = symbol;
    return true;
  }

  return false;
}
