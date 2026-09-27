import assert from 'node:assert/strict';
import { statSync } from 'node:fs';
import { join } from 'node:path';

// Strips ANSI color codes from `tree-sitter parse`'s terminal output.
export function stripAnsi(text) {
  return text.replace(/\u001b\[[0-9;]*m/g, '');
}

// Finds the locally installed tree-sitter CLI binary, checked under each
// given directory in turn, falling back to a bare `tree-sitter` lookup on
// PATH if none has one. Shared by every tool here that shells out to the
// CLI, so there is one place that knows the Windows/POSIX binary names.
export function resolveTreeSitterCli(...dirs) {
  for (const dir of dirs) {
    for (const name of ['tree-sitter.exe', 'tree-sitter']) {
      const path = join(dir, 'node_modules/tree-sitter-cli', name);
      if (statSync(path, { throwIfNoEntry: false })?.isFile()) return path;
    }
  }
  return 'tree-sitter';
}

export function fixtureResult(proc) {
  assert(!proc.error, `Could not run parser: ${proc.error?.message}`);
  assert.equal(proc.signal, null, `Parser terminated by ${proc.signal}`);
  assert.equal(proc.status, 0, `Parser exited ${proc.status}: ${proc.stderr}`);
  const tree = stripAnsi(proc.stdout);
  assert(/^\(source_file\s/m.test(tree), 'Parser did not return a source_file tree');
  return {
    errors: (tree.match(/\b(ERROR|MISSING)\b/g) || []).length,
    zeroWidth: (tree.match(/\[([0-9]+), ([0-9]+)\] - \[\1, \2\]/g) || []).length,
  };
}
