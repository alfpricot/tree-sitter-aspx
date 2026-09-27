import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import test from 'node:test';
import { canAttest, prepareNpm, releaseVersion, resolveRelease } from './release-policy.mjs';

test('npm provenance requires both the release tag and verified source commit', () => {
  assert(canAttest('v0.1.0', 'abc', 'refs/tags/v0.1.0', 'abc'));
  assert(!canAttest('v0.1.0', 'abc', 'refs/heads/main', 'abc'));
  assert(!canAttest('v0.1.0', 'abc', 'refs/tags/v0.1.0', 'def'));
  assert(!canAttest('v0.1.0', 'abc', 'refs/tags/v0.1.1', 'abc'));
  assert(!canAttest('v0.1.0', undefined, 'refs/tags/v0.1.0', undefined));
});

test('release tags must be stable versions matching the root package', () => {
  const manifest = '[workspace]\nmembers = []\n[package]\nversion = "0.1.0"\n';
  assert.equal(releaseVersion('v0.1.0', manifest), '0.1.0');
  for (const tag of ['main', '--help', 'v01.1.0', 'v0.1.1', 'v0.1.0\n', 'v0.1.0-rc.1']) {
    assert.throws(() => releaseVersion(tag, manifest));
  }
});

test('resolve only real tags on main and prepare consistent npm metadata', () => {
  const root = mkdtempSync(join(tmpdir(), 'aspx-release-'));
  const run = (command, args, options) => execFileSync(command, args, { ...options, cwd: root, stdio: ['ignore', 'pipe', 'pipe'] });
  const git = (...args) => run('git', args, { encoding: 'utf8' }).trim();
  try {
    git('init', '-b', 'main');
    writeFileSync(join(root, 'Cargo.toml'), '[package]\nname = "example"\nversion = "0.1.0"\n');
    git('add', 'Cargo.toml');
    const initial = git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit-tree', git('write-tree'), '-m', 'initial');
    git('update-ref', 'HEAD', initial);
    git('update-ref', 'refs/remotes/origin/main', 'HEAD');
    git('update-ref', 'refs/tags/v0.1.0', 'HEAD');
    assert.equal(resolveRelease('v0.1.0', run), git('rev-parse', 'HEAD'));
    git('branch', 'v0.1.2');
    assert.throws(() => resolveRelease('v0.1.2', run));
    git('update-ref', 'refs/tags/v0.1.3', 'HEAD');
    assert.throws(() => resolveRelease('v0.1.3', run));
    writeFileSync(join(root, 'Cargo.toml'), '[package]\nversion = "0.1.2"\n');
    git('add', 'Cargo.toml');
    const next = git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit-tree', git('write-tree'), '-p', initial, '-m', 'unpromoted');
    git('update-ref', 'HEAD', next);
    git('update-ref', 'refs/tags/v0.1.2', 'HEAD');
    assert.throws(() => resolveRelease('v0.1.2', run));
    writeFileSync(join(root, 'package.json'), '{"name":"tree-sitter-aspx","version":"0.0.0"}');
    writeFileSync(join(root, 'tree-sitter.json'), '{"metadata":{"version":"0.0.0"}}');
    prepareNpm('v0.1.2', pathToFileURL(root + '/'));
    const pkg = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
    assert.equal(pkg.version, '0.1.2');
    assert.equal(pkg.name, 'tree-sitter-aspx');
    assert.equal(JSON.parse(readFileSync(join(root, 'tree-sitter.json'), 'utf8')).metadata.version, '0.1.2');
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
