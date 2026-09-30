import {defineConfig} from '../frontend/node_modules/@playwright/test/index';
export default defineConfig({testDir:'.',testMatch:'website.spec.ts',use:{baseURL:'http://localhost:18103',headless:true},reporter:'line',outputDir:'../frontend/test-results'});
