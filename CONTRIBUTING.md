# Contributing to tree-sitter-aspx

A tree-sitter grammar for ASP.NET Web Forms markup, with Rust and Node bindings.

## Before you start

1. Branch from **`dev`**. `main` is the production branch, updated by release pull requests.
2. Read [ARCHITECTURE.md](ARCHITECTURE.md) for how the grammar, scanner, queries, and bindings fit together.
3. Use Node.js 24 (see `.nvmrc`) and Rust 1.90 or newer for the test suite. The published crate only needs 1.85, but the `tree-sitter` dev-dependency needs 1.90.

## Local setup

The exact CLI version is declared in `package.json` and resolved by `package-lock.json`, and CI uses the same lockfile. When changing the CLI dependency, update its `allowScripts` entry before installing, then regenerate the lockfile.

```sh
npm ci --ignore-scripts
npm rebuild tree-sitter-cli
npx --no-install tree-sitter generate
cargo build
```

`npm ci --ignore-scripts` skips the package's own `node-gyp-build` install script, which needs a C toolchain that grammar and lint work does not.

The generated parser (`src/parser.c`, `src/grammar.json`, and `src/node-types.json`) is committed. Regenerate and commit the diff after any `grammar.js` change. A failed generation leaves the prior parser in place, so confirm that generation succeeded before trusting a subsequent test run.

## Validation before a pull request

```sh
CC=gcc CXX=g++ npx --no-install tree-sitter test
node tools/node-kinds.mjs --check
node tools/fixture-check.mjs
node tools/fixture-leak-grep.mjs
node tools/package-check.mjs
node --test tools/*.test.mjs
npm run lint
npm run format:check
cargo fmt --all --check
cargo clippy --workspace --all-targets --all-features -- -D warnings
cargo test
```

- `test/corpus/` holds per-construct tree assertions. `test/fixtures/` holds larger synthetic pages checked for `ERROR`, `MISSING`, and zero-width nodes.
- `node tools/corpus-check.mjs <directory>` decodes UTF-8 BOM and UTF-16 files and applies the same check to a local Web Forms tree. Do not commit pages copied from a private application; fixtures stay synthetic.
- `npm run lint` and `npm run format:check` cover `grammar.js`, `tools/*.mjs`, and `bindings/node/*.js`; `npm run format` applies formatting. The generated parser under `src/` is excluded. `oxlint` and `oxfmt` are devDependencies, configured by `.oxlintrc.json` and `.oxfmtrc.json`.
- CI also runs `npm test`, which builds the native Node addon and needs a C/C++ toolchain, plus the Node binding on Windows when binding files change.

## Pull requests

- Open pull requests against **`dev`**.
- Use a Conventional Commit title (`feat:`, `fix:`, `docs:`, `chore:`, `test:`, and the other types CI accepts).
- Keep each pull request to one change, and link the issue it resolves in the body.
- Regenerate and commit the parser and node-kind snapshot with any grammar change; CI rejects drift in either artifact.
- Add a corpus case or extend a fixture for every newly supported construct.
- Keep node-kind renames for a breaking release: consumers match these names at runtime.
- Keep the full Rust verification suite warning-free, and pass `npm run lint` and `npm run format:check`.

## Documentation

| Document | Covers |
| --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Components, build, test, and release flow |
| [docs/EMBEDDED_CODE.md](docs/EMBEDDED_CODE.md) | C# and VB.NET node kinds and injection rules |
| [SECURITY.md](SECURITY.md) | Reporting a vulnerability |
