// Legacy Web Forms pages are often saved as UTF-16 or with a UTF-8 BOM, but
// the tree-sitter CLI reads bytes as UTF-8. decodeWebForms returns UTF-8
// bytes for any of those encodings: UTF-16 LE or BE with a BOM, UTF-16 LE
// without one (detected from NUL bytes in the odd positions of an ASCII
// page), and UTF-8 with or without a BOM.
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
