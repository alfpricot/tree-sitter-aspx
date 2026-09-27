#!/usr/bin/env node
// Rejects connection-string and credential shapes in tracked text files.
// Fixtures are synthetic; a hit means a copied page was not scrubbed.
// Patterns are assembled so this file does not contain the phrases it
// searches for, and this file is not scanned.
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
const needles = [
  ['data', 'source'],
  ['initial', 'catalog'],
  ['user', 'id'],
  ['password'],
  ['pwd'],
].map((words) => new RegExp(words.join('\\s+') + '\\s*=', 'i'));

const binary = new Set(['.png', '.jpg', '.jpeg', '.gif', '.webp', '.ico', '.wasm', '.node', '.exe', '.dll', '.pdb']);
const tracked = execFileSync('git', ['ls-files', '-z'], { encoding: 'utf8' }).split('\0').filter(Boolean);
let hits = 0;
for (const path of tracked) {
  const full = path.replaceAll('\\', '/');
  if (full.endsWith('tools/fixture-leak-grep.mjs')) continue;
  const dot = full.lastIndexOf('.');
  if (dot !== -1 && binary.has(full.slice(dot).toLowerCase())) continue;
  let text;
  try {
    text = readFileSync(path, 'utf8');
  } catch {
    continue;
  }
  if (text.includes('\0')) continue;
  for (const needle of needles) {
    if (needle.test(text)) {
      hits++;
      console.error(`${full}: matched ${needle}`);
      break;
    }
  }
}
if (hits) {
  console.error(`${hits} file(s) contain a connection-string or credential shape`);
  process.exit(1);
}
console.log('no connection-string or credential shapes in tracked text');
