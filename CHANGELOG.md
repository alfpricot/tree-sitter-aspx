# Changelog

All notable changes to this project will be documented in this file.

## 1.0.0 (2026-11-05)

### Breaking Changes

* **grammar:** read a bracket followed by whitespace as text (#37)
* **grammar:** label server scripts by their own language and bound runat (#36)

### Features

* **grammar:** imply the end tags HTML lets a page omit (#35)
* **grammar:** parse nuggets in script, style, and tag bodies (#32)

### Bug Fixes

* **queries:** match runat=server in any casing and test the queries (#34)
* **grammar:** label handler code and parse its script elements (#33)
* **grammar:** harden scanner recovery and Language detection (#31)
* **ci:** cover C sources in the Windows check and drop dead release asset (#30)

## 0.1.3 (2026-10-29)

### Bug Fixes

* **grammar:** stop text tokens from starting at whitespace (#24)

## [0.1.2](https://github.com/alfpricot/tree-sitter-aspx/compare/v0.1.1...v0.1.2) - 2026-10-04

### Other

- *(release)* promote dev to main — release pipeline reference and Wasm build fix ([#14](https://github.com/alfpricot/tree-sitter-aspx/pull/14))
- *(release)* make the release pipeline the shared reference and fix the Wasm build ([#13](https://github.com/alfpricot/tree-sitter-aspx/pull/13))

## [0.1.1](https://github.com/alfpricot/tree-sitter-aspx/compare/v0.1.0...v0.1.1) - 2026-10-04

### Other

- *(deps)* update eslint and node-gyp ([#8](https://github.com/alfpricot/tree-sitter-aspx/pull/8))
- align repository tooling with the shared grammar repo standard ([#6](https://github.com/alfpricot/tree-sitter-aspx/pull/6))

## 0.1.0

- Initial public release of the ASP.NET Web Forms grammar.
