import assert from 'node:assert/strict';
import test from 'node:test';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';
import {
  canSkipEditedBuild,
  changedPaths,
  docsOnly,
  needsWindowsBinding,
  verifyResults,
  versionsAgree,
} from './ci-policy.mjs';

test('Node packaging changes receive Windows binding verification before merge', () => {
  for (const path of [
    'bindings/node/index.js',
    'bindings/node/index.d.ts',
    'binding.gyp',
    'package.json',
    'package-lock.json',
    '.github/workflows/verify.yml',
  ])
    assert(needsWindowsBinding([path]));
  assert(!needsWindowsBinding(['README.md', 'grammar.js']));
});

test('renames include the removed source path when classifying prose', () => {
  const files = changedPaths('base', 'head', (command, args) => {
    assert.equal(command, 'git');
    assert.deepEqual(args, ['diff', '--no-renames', '--name-only', '-z', 'base...head']);
    return 'docs/grammar.md\0grammar.js\0';
  });
  assert(!docsOnly(files));
});

test('only explicit prose paths skip builds', () => {
  assert(docsOnly(['README.md', 'docs/consumer-integration.md']));
  for (const files of [
    [],
    ['grammar.js'],
    ['README.md', 'package.json'],
    ['test/fixtures/page.aspx'],
    ['.github/workflows/verify.yml'],
  ])
    assert(!docsOnly(files));
});

function results(build = 'true', event = 'pull_request') {
  return Object.fromEntries(
    ['changes', 'verify', 'consumer-pin', 'node-binding', 'pr-title'].map((job) => [
      job,
      {
        result:
          (job === 'pr-title' && event !== 'pull_request') ||
          (['verify', 'consumer-pin', 'node-binding'].includes(job) && build === 'false')
            ? 'skipped'
            : 'success',
        ...(job === 'changes' ? { outputs: { build } } : {}),
      },
    ]),
  );
}

test('aggregate accepts only deliberate skips', () => {
  for (const event of ['pull_request', 'push', 'workflow_dispatch']) {
    for (const build of ['true', 'false']) {
      const valid = results(build, event);
      verifyResults(valid, event, false);
      assert.throws(() => verifyResults(valid, event, true));
      for (const job of Object.keys(valid)) {
        for (const result of ['failure', 'cancelled', 'skipped']) {
          if (valid[job].result === result) continue;
          assert.throws(() =>
            verifyResults({ ...valid, [job]: { ...valid[job], result } }, event, false),
          );
        }
      }
    }
  }
});

test('canSkipEditedBuild only skips a title/body edit whose prior run already verified this head SHA', () => {
  assert(canSkipEditedBuild({ action: 'edited', changes: {} }, true));
  assert(!canSkipEditedBuild({ action: 'edited', changes: {} }, false));
  assert(!canSkipEditedBuild({ action: 'edited', changes: { base: { from: 'main' } } }, true));
  assert(!canSkipEditedBuild({ action: 'synchronize', changes: {} }, true));
});

test('versionsAgree passes when all three manifests match', () => {
  versionsAgree(
    '[package]\nversion = "0.1.0"\n',
    '{"version":"0.1.0"}',
    '{"metadata":{"version":"0.1.0"}}',
  );
});

test('versionsAgree rejects a package.json or tree-sitter.json version drift', () => {
  assert.throws(() =>
    versionsAgree(
      '[package]\nversion = "0.1.0"\n',
      '{"version":"0.1.1"}',
      '{"metadata":{"version":"0.1.0"}}',
    ),
  );
  assert.throws(() =>
    versionsAgree(
      '[package]\nversion = "0.1.0"\n',
      '{"version":"0.1.0"}',
      '{"metadata":{"version":"0.1.1"}}',
    ),
  );
});

test('Cargo.toml, package.json, and tree-sitter.json agree on the current version', () => {
  const root = fileURLToPath(new URL('..', import.meta.url));
  versionsAgree(
    readFileSync(join(root, 'Cargo.toml'), 'utf8'),
    readFileSync(join(root, 'package.json'), 'utf8'),
    readFileSync(join(root, 'tree-sitter.json'), 'utf8'),
  );
});
