# Security policy

## Reporting a vulnerability

Found a parser crash or hang for some input? Include the input, or a minimized version, and the binding used in a normal GitHub issue.

For an issue that could affect downstream consumers before a fix is available, do not open a public issue. Use [GitHub's private vulnerability reporting](https://github.com/alfpricot/tree-sitter-aspx/security/advisories/new) and include what you found, how to reproduce it, and the impact you expect.

## Scope

This policy covers the grammar, the external scanner, and the Node and Rust bindings in this repository, and the packages published from it. The `tree-sitter` runtime and the applications that embed this parser have their own projects and policies.

## Practices

- Test fixtures are synthetic. Never commit a page copied from a private application; `tools/fixture-leak-grep.mjs` rejects connection-string and credential shapes in tracked text.
- CI scans the working tree and the Git history for secrets on every pull request.
- Dependencies are updated through Dependabot pull requests, and `package-lock.json` pins the exact npm versions CI uses.
