import { defineConfig } from 'vite';

export default defineConfig({
    // Relative asset URLs so the built bundle also works from a plain static
    // file server subpath; dev-time tooling stays per DEC-006 decision 6
    // (tentative until M5).
    base: './',
});
