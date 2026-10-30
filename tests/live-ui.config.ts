import {defineConfig} from '../frontend/node_modules/@playwright/test/index';
export default defineConfig({testDir: '.', testMatch: 'live-ui.spec.ts', timeout: 30000,
  use: {baseURL: 'http://127.0.0.1:18119', headless: true}, reporter: 'line', outputDir: '../frontend/test-results/live-ui',
  webServer: {command: 'npm run dev --prefix ../frontend -- --port 18119', url: 'http://127.0.0.1:18119', reuseExistingServer: true},
});
