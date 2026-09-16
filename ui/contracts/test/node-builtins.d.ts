/// Ambient declarations for the Node builtin the golden-vector suite needs to
/// read the shared vectors file (tests/runtime/data/ipc_protocol_golden.json).
/// The contracts package compiles with `types: []` (browser-facing surface,
/// no @types/node installed); vitest executes the tests on real Node, so only
/// the narrow read-only surface below is declared instead of pulling the full
/// Node type set into the package.

declare module 'node:fs' {
    export function readFileSync(path: string | URL, encoding: 'utf8'): string;
}
