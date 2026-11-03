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
  STRAY_END_TAG_NAME,
};

enum Language {
  LANGUAGE_CSHARP = 0,
  LANGUAGE_VB = 1,
};

// Open elements as lowercase names. Only the innermost TAG_STACK_MAX names
// are kept, in a ring indexed by depth; positions below `known_from` (and
// any name the serialization buffer could not hold) are unknown. Names
// longer than TAG_NAME_MAX are cut to that length. An unknown name never
// matches, and no end tag is implied while the innermost name is unknown,
// so very deep pages fall back to pairing end tags by position.
#define TAG_STACK_MAX 64
#define TAG_NAME_MAX 31

typedef struct {
  uint8_t language;
  uint16_t depth;
  uint16_t known_from;
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
    scanner->known_from = 1;
  }
  return scanner;
}

void tree_sitter_aspx_external_scanner_destroy(void *payload) {
  ts_free(payload);
}

// Layout: language, depth (two bytes, little-endian), the number of names
// stored, then each stored name as a length byte and its characters,
// innermost first. Names that do not fit are restored as unknown.
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
  for (unsigned d = scanner->depth; d >= scanner->known_from && d > 0 && stored < 255; d--) {
    const char *name = scanner->names[(d - 1) % TAG_STACK_MAX];
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
  scanner->known_from = 1;
  if (length == 0) {
    return;
  }
  scanner->language = (uint8_t)buffer[0];
  if (length < 4) {
    return;
  }
  scanner->depth = (uint16_t)((uint8_t)buffer[1] | ((uint8_t)buffer[2] << 8));
  unsigned stored = (uint8_t)buffer[3];
  unsigned offset = 4;
  unsigned restored = 0;
  for (unsigned d = scanner->depth; restored < stored && d > 0 && offset < length; d--) {
    unsigned name_length = (uint8_t)buffer[offset++];
    if (name_length > TAG_NAME_MAX || offset + name_length > length) {
      break;
    }
    char *slot = scanner->names[(d - 1) % TAG_STACK_MAX];
    for (unsigned i = 0; i < name_length; i++) {
      slot[i] = buffer[offset++];
    }
    slot[name_length] = '\0';
    restored++;
  }
  scanner->known_from = (uint16_t)(scanner->depth - restored + 1);
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
// Keep in step with VOID_TAGS in grammar.js; tools/scanner-tags.test.mjs
// compares the two.
static const char *const RESERVED_TAGS[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", "script", "style",
};

// Start tags that close an open `p` in button scope, per the HTML parsing
// rules (li, dt, and dd close one too, after their own check).
static const char *const CLOSES_P[] = {
    "address", "article", "aside", "blockquote", "details", "dialog", "div",
    "dl", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2",
    "h3", "h4", "h5", "h6", "header", "hgroup", "hr", "main", "menu", "nav",
    "ol", "p", "pre", "section", "table", "ul", "li", "dt", "dd",
};

// Elements that stop the search for an open `p` (HTML's button scope).
static const char *const BUTTON_SCOPE[] = {
    "applet", "button", "caption", "html", "marquee", "object", "table",
    "td", "template", "th",
};

// HTML's "special" elements, which stop the search for an open `li`, `dt`,
// or `dd`, except for address, div, and p.
static const char *const SPECIAL[] = {
    "applet", "article", "aside", "blockquote", "body", "button", "caption",
    "center", "colgroup", "details", "dir", "dl", "fieldset", "figcaption",
    "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "head",
    "header", "hgroup", "html", "iframe", "listing", "main", "marquee",
    "menu", "nav", "noscript", "object", "ol", "pre", "section", "select",
    "summary", "table", "tbody", "td", "template", "textarea", "tfoot", "th",
    "thead", "tr", "ul",
};

static const char *const LIST_ITEMS[] = {"li"};
static const char *const DEFINITIONS[] = {"dt", "dd"};
static const char *const CELLS[] = {"td", "th"};
static const char *const ROWS[] = {"tr"};
static const char *const SECTIONS[] = {"tbody", "thead", "tfoot"};
static const char *const OPTIONS[] = {"option"};
static const char *const OPTION_GROUPS[] = {"option", "optgroup"};
static const char *const RUBY_TEXT[] = {"rt", "rp"};
static const char *const TABLE_SCOPE[] = {"html", "table", "template"};
static const char *const SELECT_SCOPE[] = {"datalist", "select"};
static const char *const RUBY_SCOPE[] = {"ruby"};
static const char *const PARAGRAPH[] = {"p"};
static const char *const NOT_SPECIAL_FOR_ITEMS[] = {"address", "div", "p"};

// How a start tag finds the open element it closes: the names it closes,
// and the names that end the search further out. With `stop_at_special`,
// the search instead ends at any special element not in `stops`.
typedef struct {
  const char *const *closes;
  unsigned closes_count;
  const char *const *stops;
  unsigned stops_count;
  bool stop_at_special;
} CloseRule;

#define RULE(closes, stops, special) {closes, COUNT(closes), stops, COUNT(stops), special}

static bool find_rule(const char *next, CloseRule *rule) {
  static const struct {
    const char *next;
    CloseRule rule;
  } RULES[] = {
      {"li", RULE(LIST_ITEMS, NOT_SPECIAL_FOR_ITEMS, true)},
      {"dt", RULE(DEFINITIONS, NOT_SPECIAL_FOR_ITEMS, true)},
      {"dd", RULE(DEFINITIONS, NOT_SPECIAL_FOR_ITEMS, true)},
      {"td", RULE(CELLS, TABLE_SCOPE, false)},
      {"th", RULE(CELLS, TABLE_SCOPE, false)},
      {"tr", RULE(ROWS, TABLE_SCOPE, false)},
      {"tbody", RULE(SECTIONS, TABLE_SCOPE, false)},
      {"thead", RULE(SECTIONS, TABLE_SCOPE, false)},
      {"tfoot", RULE(SECTIONS, TABLE_SCOPE, false)},
      {"option", RULE(OPTIONS, SELECT_SCOPE, false)},
      {"optgroup", RULE(OPTION_GROUPS, SELECT_SCOPE, false)},
      {"rt", RULE(RUBY_TEXT, RUBY_SCOPE, false)},
      {"rp", RULE(RUBY_TEXT, RUBY_SCOPE, false)},
  };
  for (unsigned i = 0; i < COUNT(RULES); i++) {
    if (strcmp(next, RULES[i].next) == 0) {
      *rule = RULES[i].rule;
      return true;
    }
  }
  return false;
}

// The name at 1-based stack position `d`, or NULL when it is not known.
static const char *name_at(Scanner *scanner, unsigned d) {
  if (d == 0 || d > scanner->depth || d < scanner->known_from) {
    return NULL;
  }
  return scanner->names[(d - 1) % TAG_STACK_MAX];
}

static void push_tag(Scanner *scanner, const char *name) {
  if (scanner->depth == UINT16_MAX) {
    return;
  }
  scanner->depth++;
  char *slot = scanner->names[(scanner->depth - 1) % TAG_STACK_MAX];
  unsigned i = 0;
  for (; name[i] != '\0'; i++) {
    slot[i] = name[i];
  }
  slot[i] = '\0';
  if (scanner->depth - scanner->known_from + 1 > TAG_STACK_MAX) {
    scanner->known_from = (uint16_t)(scanner->depth - TAG_STACK_MAX + 1);
  }
}

static void pop_tag(Scanner *scanner) {
  if (scanner->depth > 0) {
    scanner->depth--;
  }
  if (scanner->known_from > scanner->depth + 1) {
    scanner->known_from = (uint16_t)(scanner->depth + 1);
  }
}

// Walks out from the innermost element for one `rule` closes, stopping at
// the rule's scope boundary or at an unknown name. Returns its position.
static unsigned find_closed(Scanner *scanner, const CloseRule *rule) {
  for (unsigned d = scanner->depth; d > 0; d--) {
    const char *name = name_at(scanner, d);
    if (name == NULL) {
      return 0;
    }
    if (name_in(name, rule->closes, rule->closes_count)) {
      return d;
    }
    bool listed = name_in(name, rule->stops, rule->stops_count);
    if (rule->stop_at_special ? name_in(name, SPECIAL, COUNT(SPECIAL)) && !listed : listed) {
      return 0;
    }
  }
  return 0;
}

// Whether a start tag named `next` implies the end of some open element, so
// the innermost one should end now. Each implied end pops one element and
// the scanner runs again, so this repeats until the closed element is gone.
static bool start_tag_implies_end(Scanner *scanner, const char *next) {
  CloseRule rule;
  if (find_rule(next, &rule) && find_closed(scanner, &rule) > 0) {
    return true;
  }
  if (name_in(next, CLOSES_P, COUNT(CLOSES_P))) {
    static const CloseRule P_RULE = RULE(PARAGRAPH, BUTTON_SCOPE, false);
    return find_closed(scanner, &P_RULE) > 0;
  }
  return false;
}

// The position of the innermost open element named `name`, or 0. The search
// stops at the first unknown name.
static unsigned find_open(Scanner *scanner, const char *name) {
  for (unsigned d = scanner->depth; d > 0; d--) {
    const char *open = name_at(scanner, d);
    if (open == NULL) {
      return 0;
    }
    if (strcmp(open, name) == 0) {
      return d;
    }
  }
  return 0;
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
  } else if (lexer->lookahead == '<' && name_at(scanner, scanner->depth) != NULL) {
    lexer->advance(lexer, false);
    char name[TAG_NAME_MAX + 1];
    if (lexer->lookahead == '/') {
      lexer->advance(lexer, false);
      while (is_space(lexer->lookahead)) {
        lexer->advance(lexer, false);
      }
      // An end tag for an element further out closes this one first.
      if (read_tag_name(lexer, name)) {
        unsigned d = find_open(scanner, name);
        implied = d > 0 && d < scanner->depth;
      }
    } else {
      while (is_space(lexer->lookahead)) {
        lexer->advance(lexer, false);
      }
      implied = read_tag_name(lexer, name) && start_tag_implies_end(scanner, name);
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
  while (is_space(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }
  char name[TAG_NAME_MAX + 1];
  if (!read_tag_name(lexer, name) || name_in(name, RESERVED_TAGS, COUNT(RESERVED_TAGS))) {
    return false;
  }
  lexer->mark_end(lexer);
  push_tag(scanner, name);
  lexer->result_symbol = START_TAG_NAME;
  return true;
}

// An end tag closes the innermost element when its name matches it, or when
// that name is unknown. An end tag that names no open element is stray, as
// a browser treats it: it closes nothing. Implied end tags have already
// closed anything between it and a matching element further out.
static bool scan_end_tag_name(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  while (is_space(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }
  char name[TAG_NAME_MAX + 1];
  if (!read_tag_name(lexer, name)) {
    return false;
  }
  lexer->mark_end(lexer);
  const char *top = name_at(scanner, scanner->depth);
  bool closes = false;
  if (scanner->depth > 0) {
    if (top == NULL || strcmp(top, name) == 0) {
      closes = true;
    } else if (find_open(scanner, name) == 0) {
      // No known open element has this name. When some names further out
      // are unknown, it may still be one of them, so pair it by position.
      closes = scanner->known_from > 1;
    } else {
      closes = true;
    }
  }
  if (closes && valid_symbols[END_TAG_NAME]) {
    pop_tag(scanner);
    lexer->result_symbol = END_TAG_NAME;
    return true;
  }
  if (valid_symbols[STRAY_END_TAG_NAME]) {
    lexer->result_symbol = STRAY_END_TAG_NAME;
    return true;
  }
  if (valid_symbols[END_TAG_NAME]) {
    pop_tag(scanner);
    lexer->result_symbol = END_TAG_NAME;
    return true;
  }
  return false;
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

  if (valid_symbols[END_TAG_NAME] || valid_symbols[STRAY_END_TAG_NAME]) {
    return scan_end_tag_name(lexer, scanner, valid_symbols);
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
