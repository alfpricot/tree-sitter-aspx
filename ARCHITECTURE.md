# tree-sitter-aspx architecture

tree-sitter-aspx is a [tree-sitter](https://tree-sitter.github.io/tree-sitter/) grammar for ASP.NET Web Forms markup. It turns the bytes of an `.aspx`, `.ascx`, `.master`, `.asax`, `.asmx`, or `.ashx` file into a concrete syntax tree that editors, linters, and analysis tools can query.

The grammar is written once in `grammar.js`. The tree-sitter CLI generates a C parser from it, and that generated parser is committed, so a consumer builds the library without the CLI. Two thin bindings (Node and Rust) expose the same compiled parser, and two query files tell a host how to highlight the tree and which ranges hold embedded C#, VB.NET, JavaScript, or CSS.

## What tree-sitter-aspx is not

- It is not an HTML validator. Tags are parsed structurally; an end tag closes the nearest open element whether or not the names match.
- It is not a C# or VB.NET parser. Code inside a page is exposed as opaque `c_sharp_code` and `vb_code` ranges for a host to inject a real grammar into.
- It does not cover Classic ASP, Razor, or code-behind files (`.aspx.cs`, `.aspx.vb`).

## Major components

### [grammar.js](grammar.js)

The grammar definition in the tree-sitter DSL, and the source of truth for the parse tree. Node kind names are the public interface, so renaming one is a breaking change.

### [src/](src)

`parser.c`, `grammar.json`, and `node-types.json` are generated from `grammar.js` and committed; CI regenerates them and fails on any difference. `scanner.c` is hand-written and is the external scanner: it reads code, comment, style, and handler bodies up to their terminator (`%>`, `--%>`, `</script>`, `</style>`, or end of file), which tree-sitter's regular expressions cannot express, and it records the page `Language` value so later code is labeled C# or VB.NET.

### [queries/](queries)

`highlights.scm` for syntax highlighting and `injections.scm`, which maps code nodes to `c_sharp`, `vb`, JavaScript, or CSS. [docs/EMBEDDED_CODE.md](docs/EMBEDDED_CODE.md) describes the injection rules.

### [bindings/node](bindings/node)

A Node-API addon (`binding.cc`, built through the root `binding.gyp`) that exports `name` and `language`. `index.js` adds `nodeTypeInfo` from `src/node-types.json` and the two query strings, `index.d.ts` types the export, and `binding_test.js` holds its tests.

### [bindings/rust](bindings/rust)

The `tree-sitter-aspx` crate. `build.rs` compiles the committed C sources with the `cc` crate, and `lib.rs` exports `LANGUAGE`, `NODE_TYPES`, `HIGHLIGHTS_QUERY`, and `INJECTIONS_QUERY`. Its only runtime dependency is `tree-sitter-language`.

### [test/](test)

`corpus/` holds per-construct tree assertions for `tree-sitter test`. `fixtures/` holds synthetic pages that must parse with no `ERROR`, `MISSING`, or zero-width nodes. `node-kinds.txt` is the committed list of named node kinds.

### [tools/](tools)

Node scripts that CI and contributors run: `node-kinds.mjs` (node-kind snapshot), `fixture-check.mjs` and `corpus-check.mjs` (parse checks, the latter decoding UTF-16 pages), `fixture-leak-grep.mjs` (rejects connection-string shapes), `package-check.mjs` (published package contents), and `ci-policy.mjs` and `release-policy.mjs` with their tests (what CI must run and which release tags are valid).

## External systems and data flow

The library reads no files and opens no connections at run time. A host hands it UTF-8 bytes; the external scanner and generated parser return a syntax tree; the host runs `highlights.scm` or `injections.scm` over that tree.

The build and release process touches these services:

- **crates.io** receives the Rust crate.
- **npm** receives the Node package; **GitHub Packages** receives a copy scoped to the repository owner, because GitHub Packages requires the scope to match the owner.
- **GitHub Releases** receives a WASM build for `web-tree-sitter` consumers, plus `test/node-kinds.txt` and `versions.json` as the node-kind stability record.

## Build, test, release, and deploy

**Pull requests and pushes to `dev` and `main`** run the `Verify` workflow (`verify.yml`). Its jobs scan for secrets (Gitleaks), lint workflows (actionlint), validate YAML, check the pull request title against Conventional Commits, regenerate the parser and fail if it differs from the committed copy, lint and format-check the JavaScript, run the corpus tests, check fixtures, run `cargo fmt`, `cargo clippy`, and `cargo test`, verify the package contents, rebuild against tree-sitter 0.26 (`consumer-pin`), and build and test the Node binding on Linux (and on Windows when binding files changed). A final `ci` job rolls these up into the single `Verify` status that branch rulesets require.

**Releases** are cut from `main`. `gala-release.yml` runs on each push to `main` and runs gala (configured in `.gala.toml`), which computes the next version from Conventional Commits, bumps the version in `Cargo.toml`, `package.json`, and `tree-sitter.json`, refreshes `package-lock.json`, regenerates the parser (whose metadata embeds the `tree-sitter.json` version), writes the `CHANGELOG.md` entry, commits and tags it, pushes both over a deploy key, and fast-forwards `dev` onto the release commit. When gala cut a release, the workflow then dispatches `release.yml` for the new tag. `release.yml` first re-verifies the tagged commit (the tag must be on `main` and match the version in `Cargo.toml`, `package.json`, and `tree-sitter.json`), builds Node prebuilds for Linux and Windows and the WASM grammar, then publishes the crate, the npm package, the GitHub Packages copy, and the GitHub Release. The npm and GitHub Packages archives carry the release version in both `package.json` and `tree-sitter.json` as committed in the release commit.

**Maintenance:** `dependabot.yml` updates Cargo, npm, and GitHub Actions dependencies against `dev`, and `cache-cleanup.yml` deletes Actions caches left by closed pull requests and deleted branches.

There is no deployment: the repository ships a library and its bindings.

## Where things live

| Path | Holds |
| --- | --- |
| `grammar.js` | Grammar rules |
| `src/` | Generated parser, node type data, and the hand-written external scanner |
| `queries/` | Highlight and injection queries |
| `bindings/node/`, `binding.gyp` | Node-API addon and its tests |
| `bindings/rust/`, `Cargo.toml` | Rust crate |
| `test/` | Corpus tests, fixtures, node-kind snapshot |
| `tools/` | CI and release check scripts |
| `docs/` | Reference documents moved out of the README |
| `.github/workflows/` | Verify, release, and maintenance workflows |
| `.oxlintrc.json`, `.oxfmtrc.json` | JavaScript lint and format configuration |
| `.gala.toml` | Release tooling configuration |
