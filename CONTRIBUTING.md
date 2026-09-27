# Contributing

## Build

The exact CLI version is declared in `package.json` and resolved by `package-lock.json`.
CI uses this same lockfile. When changing the CLI dependency, update its `allowScripts`
entry before installing, then regenerate the lockfile.

```sh
npm ci --ignore-scripts
npm rebuild tree-sitter-cli
npx --no-install tree-sitter generate
cargo build
```

The generated parser (`src/parser.c`, `src/grammar.json`, and
`src/node-types.json`) is committed. Regenerate and commit the diff after any
`grammar.js` change. A failed generation leaves the prior parser in place, so
confirm that generation succeeded before trusting a subsequent test run.

## Test

```sh
CC=gcc CXX=g++ npx --no-install tree-sitter test
node tools/node-kinds.mjs --check
node tools/fixture-check.mjs
node tools/fixture-leak-grep.mjs
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test
```

`test/corpus/` holds per-construct tree assertions. `test/fixtures/` holds
larger synthetic pages checked for `ERROR`, `MISSING`, and zero-width nodes.
`node tools/corpus-check.mjs <directory>` decodes UTF-8 BOM and UTF-16 files
and applies the same check to a local Web Forms tree. Do not commit pages
copied from a private application; fixtures stay synthetic.

## Pull requests

- Regenerate and commit the parser and node-kind snapshot with any grammar
  change; CI rejects drift in either artifact.
- Add a corpus case or extend a fixture for every newly supported construct.
- Keep node-kind renames for a breaking release: consumers match these names
  at runtime.
- Keep the full Rust verification suite warning-free.
