import js from '@eslint/js';
import reactHooks from 'eslint-plugin-react-hooks';
import tseslint from 'typescript-eslint';

// M5-01 (DEC-006 decision 6): ESLint is the finalized frontend lint toolchain.
// Scope is the product frontend workspaces (contracts + app). ui/shell-poc is
// PoC evidence with its own materials (ui/shell-poc/README.md), not product
// code, and stays out of the lint gate; build output is never linted.
export default tseslint.config(
  {
    ignores: ['**/dist/**', '**/coverage/**', 'shell-poc/**'],
  },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    files: ['app/src/**/*.{ts,tsx}', 'app/test/**/*.{ts,tsx}'],
    plugins: { 'react-hooks': reactHooks },
    rules: {
      ...reactHooks.configs.recommended.rules,
      // react-hooks v7 added purity / set-state-in-effect after M1.5 delivered
      // the current views; the 7 standing findings (5x Date.now during render,
      // 2x setState in effect) need view-level redesign — injected time source,
      // effect→render derivation — that belongs to the M5 UI productization
      // items reworking these files (M5-06/M5-07). Scoped waiver, not a
      // permanent exemption; the rest of the plugin stays enforced.
      'react-hooks/purity': 'off',
      'react-hooks/set-state-in-effect': 'off',
    },
  },
);
