import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { decodeWebForms } from './web-forms-encoding.mjs';

// The fixture page plus a line with non-ASCII text, so each decoding is
// checked on multi-byte characters as well as ASCII markup.
const text =
  readFileSync(new URL('../test/fixtures/encoded.aspx', import.meta.url), 'utf8') +
  '<p>Caf\u00e9 \u2014 na\u00efve</p>\n';
const page = Buffer.from(text, 'utf8');

function utf16be(source) {
  const bytes = Buffer.from(source, 'utf16le');
  for (let i = 0; i + 1 < bytes.length; i += 2) {
    [bytes[i], bytes[i + 1]] = [bytes[i + 1], bytes[i]];
  }
  return bytes;
}

test('UTF-8 without a BOM is returned unchanged', () => {
  assert.equal(decodeWebForms(page), page);
});

test('a UTF-8 BOM is removed', () => {
  const bytes = Buffer.concat([Buffer.from([0xef, 0xbb, 0xbf]), page]);
  assert.equal(decodeWebForms(bytes).toString('utf8'), text);
});

test('UTF-16 LE with a BOM is decoded to UTF-8', () => {
  const bytes = Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(text, 'utf16le')]);
  assert.equal(decodeWebForms(bytes).toString('utf8'), text);
});

test('UTF-16 BE with a BOM is decoded to UTF-8', () => {
  const bytes = Buffer.concat([Buffer.from([0xfe, 0xff]), utf16be(text)]);
  assert.equal(decodeWebForms(bytes).toString('utf8'), text);
});

test('UTF-16 LE without a BOM is detected from its NUL bytes', () => {
  assert.equal(decodeWebForms(Buffer.from(text, 'utf16le')).toString('utf8'), text);
});

test('short or empty input is returned unchanged', () => {
  for (const bytes of [Buffer.alloc(0), Buffer.from('<%'), Buffer.from([0x3c, 0x00])]) {
    assert.equal(decodeWebForms(bytes), bytes);
  }
});
