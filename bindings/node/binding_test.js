import assert from 'node:assert';
import { test } from 'node:test';
import Parser from 'tree-sitter';

test('can load grammar', async () => {
  const parser = new Parser();
  await assert.doesNotReject(async () => {
    const { default: language } = await import('./index.js');
    parser.setLanguage(language);
  });
});

test('exports node type metadata', async () => {
  const { default: language } = await import('./index.js');
  assert(Array.isArray(language.nodeTypeInfo));
  assert(language.nodeTypeInfo.some((node) => node.type === 'document'));
});

test('exports the aspx language name', async () => {
  const { default: language } = await import('./index.js');
  assert.equal(language.name, 'aspx');
});

test('parses a page directive without error', async () => {
  const { default: language } = await import('./index.js');
  const parser = new Parser();
  parser.setLanguage(language);
  const tree = parser.parse('<%@ Page Language="C#" Inherits="Example.Page" %>');
  assert.equal(tree.rootNode.hasError, false);
  assert.equal(tree.rootNode.type, 'source_file');
});
