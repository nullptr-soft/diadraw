import js from '@eslint/js';
import globals from 'globals';
import tseslint from 'typescript-eslint';

export default tseslint.config(
  {
    ignores: [
      '**/dist/**',
      '**/node_modules/**',
      '**/.turbo/**',
      '**/coverage/**',
      // Build output: the C++ build, the core as WebAssembly, Tauri's target folder.
      'src/build/**',
      'src/wasm/src/core/diadraw.mjs',
      'src/apps/desktop/src-tauri/target/**',
      'src/apps/confluence/dist-dev/**',
      'src/apps/confluence-drawio/dist-dev/**',
      // draw.io, downloaded by src/apps/confluence-drawio/scripts/fetch-drawio.mjs.
      'src/apps/confluence-drawio/vendor/**',
      // The website's Astro types.
      'site/.astro/**',
    ],
  },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    languageOptions: {
      globals: { ...globals.node },
    },
    rules: {
      '@typescript-eslint/consistent-type-imports': 'error',
      '@typescript-eslint/no-unused-vars': [
        'error',
        { argsIgnorePattern: '^_', varsIgnorePattern: '^_', ignoreRestSiblings: true },
      ],
    },
  },
  {
    files: [
      'src/apps/web/**/*.{ts,tsx}',
      'src/markdown/src/{browser,embed}.ts',
      'src/markdown/examples/**',
      'src/apps/confluence/src/**/*.ts',
      'src/apps/confluence/static/**',
      'src/apps/confluence-drawio/src/**/*.ts',
      'src/apps/confluence-drawio/static/**',
    ],
    languageOptions: {
      globals: { ...globals.browser },
    },
  },
);
