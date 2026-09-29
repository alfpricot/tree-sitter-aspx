import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { appendFileSync, readFileSync } from 'node:fs';

export function docsOnly(files) {
  // Only prose outside source/test trees can skip compilation.
  return files.length > 0 && files.every((path) => /^(?:[^/]+\.md|docs\/.*\.md)$/.test(path));
}

export function needsWindowsBinding(files) {
  return files.some((path) =>
    /^(?:bindings\/node\/|binding\.gyp$|package(?:-lock)?\.json$|\.github\/workflows\/verify\.yml$)/.test(
      path,
    ),
  );
}

export function changedPaths(base, head, run = execFileSync) {
  return run('git', ['diff', '--no-renames', '--name-only', '-z', `${base}...${head}`], {
    encoding: 'utf8',
  })
    .split('\0')
    .filter(Boolean);
}

// A pure title/body edit carries no new commit, so the head SHA is
// unchanged from whatever run last built it — but only when that prior
// run actually succeeded. Skipping on the strength of "no code diff"
// alone lets an edit made after a failed or still-in-flight build for
// this exact SHA post a green result that overwrites it. A base-branch
// retarget always needs a full run regardless of `priorVerified`.
export function canSkipEditedBuild(event, priorVerified) {
  return event.action === 'edited' && !event.changes?.base && priorVerified;
}

export function versionsAgree(cargoToml, packageJson, treeSitterJson) {
  const cargoVersion = cargoToml.match(/^version\s*=\s*"([^"]+)"\s*$/m)?.[1];
  const npmVersion = JSON.parse(packageJson).version;
  const treeSitterVersion = JSON.parse(treeSitterJson).metadata?.version;
  assert.equal(
    npmVersion,
    cargoVersion,
    `package.json version "${npmVersion}" must match Cargo.toml version "${cargoVersion}"`,
  );
  assert.equal(
    treeSitterVersion,
    cargoVersion,
    `tree-sitter.json metadata.version "${treeSitterVersion}" must match Cargo.toml version "${cargoVersion}"`,
  );
}

export function verifyResults(needs, event, draft) {
  assert(!draft, 'Draft pull requests require a full verification run');
  for (const [job, value] of Object.entries(needs)) {
    let expected = 'success';
    if (job === 'pr-title' && event !== 'pull_request') expected = 'skipped';
    if (
      ['verify', 'consumer-pin', 'node-binding'].includes(job) &&
      needs.changes?.outputs?.build === 'false'
    )
      expected = 'skipped';
    assert.equal(value.result, expected, `${job} must finish as ${expected}`);
  }
}

if (process.argv[2] === 'changes') {
  const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
  let build = true;
  let windows = process.env.GITHUB_REF === 'refs/heads/main';
  if (process.env.GITHUB_EVENT_NAME === 'pull_request') {
    if (canSkipEditedBuild(event, process.env.PRIOR_VERIFIED === 'true')) {
      build = false;
      windows = false;
    } else {
      const { base, head } = event.pull_request;
      const files = changedPaths(base.sha, head.sha);
      build = !docsOnly(files);
      windows = needsWindowsBinding(files);
    }
  }
  appendFileSync(process.env.GITHUB_OUTPUT, `build=${build}\nwindows=${windows}\n`);
} else if (process.argv[2] === 'verify') {
  verifyResults(
    JSON.parse(process.env.JOB_RESULTS),
    process.env.GITHUB_EVENT_NAME,
    process.env.IS_DRAFT === 'true',
  );
} else if (process.argv[2] === 'versions') {
  versionsAgree(
    readFileSync('Cargo.toml', 'utf8'),
    readFileSync('package.json', 'utf8'),
    readFileSync('tree-sitter.json', 'utf8'),
  );
  console.log('package.json and tree-sitter.json versions match Cargo.toml');
}
