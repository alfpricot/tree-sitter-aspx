#!/usr/bin/env node
// Node-kind snapshot: prints the sorted list of named node kinds the
// grammar exports (from src/node-types.json) — the public API consumers
// key on. Node kinds are committed in test/node-kinds.txt; CI regenerates
// the parser, re-extracts, and diffs, so a rename cannot land quietly
// (a rename or removal is a major version bump).
//
//   node tools/node-kinds.mjs            # print the listing
//   node tools/node-kinds.mjs --check    # diff against test/node-kinds.txt
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = fileURLToPath(new URL('..', import.meta.url));
const types = JSON.parse(readFileSync(join(repo, 'src/node-types.json'), 'utf8'));
const kinds = types
  .filter((n) => n.named)
  .map((n) => n.type)
  .sort();

if (process.argv.includes('--check')) {
  const committed = readFileSync(join(repo, 'test/node-kinds.txt'), 'utf8')
    .split(/\r?\n/).filter((l) => l && !l.startsWith('#'));
  const added = kinds.filter((k) => !committed.includes(k));
  const removed = committed.filter((k) => !kinds.includes(k));
  if (added.length || removed.length) {
    console.error('node-kind snapshot drift:');
    added.forEach((k) => console.error(`  + ${k}`));
    removed.forEach((k) => console.error(`  - ${k}`));
    console.error('(a removal or rename is a major version bump)');
    process.exit(1);
  }
  console.log(`node-kind snapshot matches (${kinds.length} kinds)`);
} else {
  for (const k of kinds) console.log(k);
}
