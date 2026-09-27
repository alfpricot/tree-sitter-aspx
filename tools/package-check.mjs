import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';

const tracked = new Set(execFileSync('git', ['ls-files', '-z'], { encoding: 'utf8' }).split('\0'));
const common = ['LICENSE', 'README.md', 'src/parser.c', 'src/scanner.c', 'queries/highlights.scm'];

function check(paths, required, prefix = '', generated = []) {
  const files = paths.map((path) => path.replaceAll('\\', '/'));
  for (const path of required) {
    assert(files.includes(path), `Package is missing ${prefix}${path}`);
  }
  for (const path of files) {
    assert(!/(^|\/)(\.git|\.worktrees|node_modules|target)(\/|$)/.test(path), `Forbidden package path: ${path}`);
    assert(generated.includes(path) || tracked.has(prefix + path), `Untracked package path: ${prefix}${path}`);
  }
  console.log(`Validated ${files.length} package entries (${prefix || 'grammar'}).`);
}

const files = execFileSync('cargo', ['package', '-p', 'tree-sitter-aspx', '--list', '--allow-dirty'], { encoding: 'utf8' }).trim().split(/\r?\n/);
check(files, [...common, 'bindings/rust/lib.rs', 'bindings/rust/build.rs'], '', ['Cargo.toml', 'Cargo.toml.orig', 'Cargo.lock', '.cargo_vcs_info.json']);

// npm.cmd requires the Windows command interpreter; all arguments are fixed.
const command = process.platform === 'win32' ? 'cmd.exe' : 'npm';
const args = process.platform === 'win32' ? ['/d', '/s', '/c', 'npm.cmd pack --dry-run --json --ignore-scripts'] : ['pack', '--dry-run', '--json', '--ignore-scripts'];
const result = JSON.parse(execFileSync(command, args, { encoding: 'utf8' }));
const packages = Array.isArray(result) ? result : Object.values(result);
assert.equal(packages.length, 1, 'Expected one npm package');
check(packages[0].files.map(({ path }) => path), [...common, 'package.json', 'binding.gyp', 'bindings/node/index.js', 'bindings/node/binding.cc', 'tree-sitter.json']);
