import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import test from 'node:test';
import {
  canAttest,
  checkVersions,
  prepareNpm,
  releaseVersion,
  resolveRelease,
  runCommand,
} from './release-policy.mjs';

const writeRelease = (root, cargo, npm = cargo, treeSitter = cargo) => {
  writeFileSync(join(root, 'Cargo.toml'), `[package]\nname = "example"\nversion = "${cargo}"\n`);
  writeFileSync(join(root, 'package.json'), `{"name":"example","version":"${npm}"}`);
  writeFileSync(join(root, 'tree-sitter.json'), `{"metadata":{"version":"${treeSitter}"}}`);
};
const GALA = '[[versionFile]]\npath = "package.json"\nkey = "version"\n';
const LEGACY_GALA = 'kind = "app"\nmanager = "cargo"\n';

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

test('package.json and tree-sitter.json must carry the Cargo version', () => {
  const root = mkdtempSync(join(tmpdir(), 'aspx-versions-'));
  const url = pathToFileURL(root + '/');
  try {
    writeRelease(root, '0.1.2');
    assert.equal(checkVersions(url), '0.1.2');
    assert.equal(checkVersions(url, 'v0.1.2'), '0.1.2');
    assert.throws(() => checkVersions(url, 'v0.1.3'));
    writeRelease(root, '0.1.2', '0.1.1');
    assert.throws(() => checkVersions(url), /package\.json/);
    writeRelease(root, '0.1.2', '0.1.2', '0.1.1');
    assert.throws(() => checkVersions(url), /tree-sitter\.json/);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('resolve only real tags on main whose version files agree', () => {
  const root = mkdtempSync(join(tmpdir(), 'aspx-release-'));
  const run = (command, args, options) =>
    execFileSync(command, args, { ...options, cwd: root, stdio: ['ignore', 'pipe', 'pipe'] });
  const git = (...args) => run('git', args, { encoding: 'utf8' }).trim();
  const commit = (message, ...parents) => {
    git('add', 'Cargo.toml', 'package.json', 'tree-sitter.json', '.gala.toml');
    const id = git(
      '-c',
      'user.name=Test',
      '-c',
      'user.email=test@example.invalid',
      'commit-tree',
      git('write-tree'),
      ...parents.flatMap((parent) => ['-p', parent]),
      '-m',
      message,
    );
    git('update-ref', 'HEAD', id);
    return id;
  };
  try {
    git('init', '-b', 'main');
    writeFileSync(join(root, '.gala.toml'), GALA);
    writeRelease(root, '0.1.0');
    const initial = commit('initial');
    git('update-ref', 'refs/remotes/origin/main', 'HEAD');
    git('update-ref', 'refs/tags/v0.1.0', 'HEAD');
    assert.equal(resolveRelease('v0.1.0', run), initial);
    git('branch', 'v0.1.2');
    assert.throws(() => resolveRelease('v0.1.2', run));
    git('update-ref', 'refs/tags/v0.1.3', 'HEAD');
    assert.throws(() => resolveRelease('v0.1.3', run));

    writeRelease(root, '0.1.4', '0.1.3');
    const stale = commit('package.json left behind', initial);
    git('update-ref', 'refs/remotes/origin/main', 'HEAD');
    git('update-ref', 'refs/tags/v0.1.4', 'HEAD');
    assert.throws(() => resolveRelease('v0.1.4', run), /package\.json/);

    writeRelease(root, '0.1.5');
    commit('unpromoted', stale);
    git('update-ref', 'refs/tags/v0.1.5', 'HEAD');
    assert.throws(() => resolveRelease('v0.1.5', run));
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('a tag cut before gala wrote the version files keeps publish-time versions', () => {
  const root = mkdtempSync(join(tmpdir(), 'aspx-legacy-'));
  const url = pathToFileURL(root + '/');
  const run = (command, args, options) =>
    execFileSync(command, args, { ...options, cwd: root, stdio: ['ignore', 'pipe', 'pipe'] });
  const git = (...args) => run('git', args, { encoding: 'utf8' }).trim();
  try {
    git('init', '-b', 'main');
    writeFileSync(join(root, '.gala.toml'), LEGACY_GALA);
    writeRelease(root, '0.1.1', '0.1.0');
    git('add', 'Cargo.toml', 'package.json', 'tree-sitter.json', '.gala.toml');
    const tagged = git(
      '-c',
      'user.name=Test',
      '-c',
      'user.email=test@example.invalid',
      'commit-tree',
      git('write-tree'),
      '-m',
      'legacy release',
    );
    git('update-ref', 'HEAD', tagged);
    git('update-ref', 'refs/remotes/origin/main', 'HEAD');
    git('update-ref', 'refs/tags/v0.1.1', 'HEAD');
    assert.equal(resolveRelease('v0.1.1', run), tagged);
    assert.equal(prepareNpm('v0.1.1', url), '0.1.1');
    assert.equal(JSON.parse(readFileSync(join(root, 'package.json'), 'utf8')).version, '0.1.1');
    assert.equal(
      JSON.parse(readFileSync(join(root, 'tree-sitter.json'), 'utf8')).metadata.version,
      '0.1.1',
    );
    assert.throws(() => prepareNpm('v0.1.2', url));

    writeFileSync(join(root, '.gala.toml'), GALA);
    writeRelease(root, '0.1.1', '0.1.0');
    assert.throws(() => prepareNpm('v0.1.1', url), /package\.json/);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('an unknown command fails instead of passing without doing anything', () => {
  assert.throws(() => runCommand('publish-npm', {}), /unknown command "publish-npm"/);
  assert.throws(() => runCommand(undefined, {}), /unknown command/);
});
