#!/usr/bin/env node
// Fixture check: parses every Web Forms fixture and reports, per file,
// ERROR/MISSING node counts and zero-width nodes (equal start and end
// positions). Exits non-zero if any file has either.
import { spawnSync } from 'node:child_process';
import { readdirSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { fixtureResult, resolveTreeSitterCli } from './fixture-result.mjs';

const repo = fileURLToPath(new URL('..', import.meta.url));
const cli = resolveTreeSitterCli(repo);
const extensions = new Set(['.aspx', '.ascx', '.master', '.asax', '.asmx', '.ashx']);
const fixtures = readdirSync(join(repo, 'test/fixtures')).filter((f) =>
  extensions.has(f.slice(f.lastIndexOf('.')).toLowerCase()),
);
assert(fixtures.length > 0, 'No Web Forms fixtures found');

let bad = 0;
for (const f of fixtures) {
  const proc = spawnSync(cli, ['parse', join('test/fixtures', f)], {
    cwd: repo,
    encoding: 'utf8',
    maxBuffer: 1 << 26,
    env: { ...process.env, CC: 'gcc', CXX: 'g++' },
  });
  try {
    const { errors, zeroWidth } = fixtureResult(proc);
    if (errors || zeroWidth) {
      bad++;
      console.log(`${f}: errors=${errors} zero-width=${zeroWidth}`);
    }
  } catch (error) {
    bad++;
    console.error(`${f}: ${error.message}`);
  }
}
console.log(
  bad
    ? `${bad}/${fixtures.length} fixture files with problems`
    : `all ${fixtures.length} fixtures parse clean (no ERROR/MISSING, no zero-width nodes)`,
);
process.exit(bad ? 1 : 0);
