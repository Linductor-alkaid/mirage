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
      // M5-01 曾对 purity / set-state-in-effect 记录性豁免（M1.5 既有视图的
      // 7 处发现）；M5-06（DEC-025）视图重做时以注入时间源（useNow）与
      // 渲染期状态调整模式清零，两规则全量生效。
    },
  },
);
