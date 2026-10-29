import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { appendFileSync, existsSync, readFileSync, writeFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

export function cargoVersion(manifest) {
  const section = manifest.split(/^\[package\]\s*$/m)[1]?.split(/^\[/m)[0];
  return section?.match(/^version\s*=\s*"([^"]+)"\s*$/m)?.[1];
}

export function releaseVersion(tag, manifest) {
  assert(
    /^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(tag),
    'Expected a stable release tag such as v0.1.1',
  );
  const version = cargoVersion(manifest);
  assert.equal(version, tag.slice(1), 'Release tag must match the root Cargo package version');
  return version;
}

// gala writes package.json and tree-sitter.json in the release commit along
// with Cargo.toml, and the generated parser embeds tree-sitter.json's version,
// so a disagreement means the commit was not cut by a release.
export function versionsMatch(version, packageJson, treeSitterJson) {
  assert.equal(
    JSON.parse(packageJson).version,
    version,
    `package.json version must match Cargo.toml version ${version}`,
  );
  assert.equal(
    JSON.parse(treeSitterJson).metadata?.version,
    version,
    `tree-sitter.json metadata.version must match Cargo.toml version ${version}`,
  );
}

// A release cut before gala wrote package.json and tree-sitter.json (its
// .gala.toml lists no [[versionFile]]) left those two files at an older
// version; the publish jobs set them from the tag for such a tag, as they did
// when it was cut, so an already published tag can still be re-dispatched.
export function writesVersionFiles(galaToml) {
  return /^\[\[versionFile\]\]\s*$/m.test(galaToml ?? '');
}

export function resolveRelease(tag, run = execFileSync) {
  // Validate before passing the ref to git; branch names and option-like input fail closed.
  releaseVersion(tag, `[package]\nversion = "${tag?.slice(1)}"\n`);
  const git = (args) => run('git', args, { encoding: 'utf8' }).trim();
  const commit = git(['rev-parse', '--verify', `refs/tags/${tag}^{commit}`]);
  git(['merge-base', '--is-ancestor', commit, 'refs/remotes/origin/main']);
  const version = releaseVersion(tag, git(['show', `${commit}:Cargo.toml`]));
  const ls = git(['ls-tree', '--name-only', commit, '--', '.gala.toml']);
  if (!writesVersionFiles(ls && git(['show', `${commit}:.gala.toml`]))) return commit;
  versionsMatch(
    version,
    git(['show', `${commit}:package.json`]),
    git(['show', `${commit}:tree-sitter.json`]),
  );
  return commit;
}

// With a tag, the working tree must be that release; without one (Verify),
// the three files only have to agree with each other.
export function checkVersions(root = pathToFileURL(process.cwd() + '/'), tag) {
  const read = (name) => readFileSync(new URL(name, root), 'utf8');
  const manifest = read('Cargo.toml');
  const version = tag === undefined ? cargoVersion(manifest) : releaseVersion(tag, manifest);
  assert(version, 'Cargo.toml has no [package] version');
  versionsMatch(version, read('package.json'), read('tree-sitter.json'));
  return version;
}

// Checks package.json and tree-sitter.json against the tag; for a legacy tag
// (see writesVersionFiles) it writes the tag's version into both instead.
export function prepareNpm(tag, root = pathToFileURL(process.cwd() + '/')) {
  const galaToml = new URL('.gala.toml', root);
  if (writesVersionFiles(existsSync(galaToml) ? readFileSync(galaToml, 'utf8') : '')) {
    return checkVersions(root, tag);
  }
  const version = releaseVersion(tag, readFileSync(new URL('Cargo.toml', root), 'utf8'));
  for (const name of ['package.json', 'tree-sitter.json']) {
    const path = new URL(name, root);
    const data = JSON.parse(readFileSync(path, 'utf8'));
    if (name === 'package.json') data.version = version;
    else data.metadata.version = version;
    writeFileSync(path, JSON.stringify(data, null, 2) + '\n');
  }
  return version;
}

export function canAttest(tag, commit, ref, sha) {
  return Boolean(commit) && ref === `refs/tags/${tag}` && sha === commit;
}

// The publish jobs run this script from main while the workflow file comes
// from the dispatched ref, so an older workflow can name a command this
// version no longer has; that must fail the job, not pass it doing nothing.
// prepare-npm is the name workflows used before check-npm.
export function runCommand(command, env = process.env) {
  if (command === 'resolve') {
    const commit = resolveRelease(env.TAG);
    const policy = execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim();
    appendFileSync(env.GITHUB_OUTPUT, `commit=${commit}\npolicy=${policy}\n`);
  } else if (command === 'check-npm' || command === 'prepare-npm') {
    prepareNpm(env.TAG);
    const provenance = canAttest(env.TAG, env.RELEASE_COMMIT, env.GITHUB_REF, env.GITHUB_SHA);
    appendFileSync(env.GITHUB_OUTPUT, `provenance=${provenance}\n`);
  } else if (command === 'versions') {
    const version = checkVersions();
    console.log(`Cargo.toml, package.json, and tree-sitter.json all carry ${version}`);
  } else {
    throw new Error(`release-policy: unknown command ${JSON.stringify(command)}`);
  }
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  runCommand(process.argv[2]);
}
