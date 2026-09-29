import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../..', import.meta.url));

const binding =
  typeof process.versions.bun === 'string'
    ? // Support `bun build --compile` by being statically analyzable enough to find the .node file at build-time
      await import(`${root}/prebuilds/${process.platform}-${process.arch}/tree-sitter-aspx.node`)
    : (await import('node-gyp-build')).default(root);

try {
  binding.nodeTypeInfo = JSON.parse(
    readFileSync(new URL('../../src/node-types.json', import.meta.url), 'utf8'),
  );
} catch {
  // node-types.json is optional metadata; the binding still works without it.
}

const queries = [
  ['HIGHLIGHTS_QUERY', `${root}/queries/highlights.scm`],
  ['INJECTIONS_QUERY', `${root}/queries/injections.scm`],
];

for (const [prop, path] of queries) {
  Object.defineProperty(binding, prop, {
    configurable: true,
    enumerable: true,
    get() {
      delete binding[prop];
      try {
        binding[prop] = readFileSync(path, 'utf8');
      } catch {
        // A missing query file leaves the property undefined.
      }
      return binding[prop];
    },
  });
}

export default binding;
