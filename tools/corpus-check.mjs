#!/usr/bin/env node
// Parse every Web Forms file under a directory. Decodes a leading UTF-8
// BOM and UTF-16 LE/BE (with or without a BOM) to UTF-8 before parsing,
// because the tree-sitter CLI reads bytes as UTF-8. Prints a count of
// ERROR and MISSING nodes per file that has any, and exits non-zero if
// any file fails. File contents are never printed.
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { fixtureResult, resolveTreeSitterCli } from './fixture-result.mjs';

const extensions = new Set(['.aspx', '.ascx', '.master', '.asax', '.asmx', '.ashx']);
const repo = fileURLToPath(new URL('..', import.meta.url));
const root = process.argv[2];
if (!root) {
  console.error('Usage: node tools/corpus-check.mjs <directory>');
  process.exit(2);
}

export function decodeWebForms(bytes) {
  if (bytes.length >= 2 && bytes[0] === 0xff && bytes[1] === 0xfe) {
    return Buffer.from(new TextDecoder('utf-16le').decode(bytes.subarray(2)));
  }
  if (bytes.length >= 2 && bytes[0] === 0xfe && bytes[1] === 0xff) {
    return Buffer.from(new TextDecoder('utf-16be').decode(bytes.subarray(2)));
  }
  if (bytes.length >= 3 && bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) {
    return bytes.subarray(3);
  }
  // UTF-16 LE without a BOM: NUL bytes in the odd positions of an ASCII page.
  if (bytes.length >= 8 && bytes[1] === 0 && bytes[3] === 0 && bytes[5] === 0) {
    return Buffer.from(new TextDecoder('utf-16le').decode(bytes));
  }
  return bytes;
}

function walk(dir, out) {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) {
      if (entry.name === 'node_modules' || entry.name === '.git') continue;
      walk(path, out);
    } else if (extensions.has(extname(entry.name).toLowerCase())) {
      out.push(path);
    }
  }
}

const files = [];
walk(root, files);
if (files.length === 0) {
  console.error(`No Web Forms files under ${root}`);
  process.exit(2);
}

const cli = resolveTreeSitterCli(repo);
const temp = mkdtempSync(join(tmpdir(), 'aspx-corpus-'));
let bad = 0;
try {
  files.forEach((path, index) => {
    const decoded = decodeWebForms(readFileSync(path));
    const tempPath = join(temp, `${index}${extname(path).toLowerCase()}`);
    writeFileSync(tempPath, decoded);
    const proc = spawnSync(cli, ['parse', tempPath], {
      cwd: repo,
      encoding: 'utf8',
      maxBuffer: 1 << 26,
      env: { ...process.env, CC: 'gcc', CXX: 'g++' },
    });
    try {
      const { errors, zeroWidth } = fixtureResult(proc);
      if (errors || zeroWidth) {
        bad++;
        console.log(`${basename(path)}: errors=${errors} zero-width=${zeroWidth}`);
      }
    } catch (error) {
      bad++;
      console.error(`${basename(path)}: ${error.message}`);
    }
  });
} finally {
  rmSync(temp, { recursive: true, force: true });
}

console.log(
  bad
    ? `${bad}/${files.length} files with problems`
    : `all ${files.length} files parse clean (no ERROR/MISSING, no zero-width nodes)`,
);
process.exit(bad ? 1 : 0);
