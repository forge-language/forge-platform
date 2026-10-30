import {defineConfig} from '../frontend/node_modules/@playwright/test/index';
export default defineConfig({testDir:'.',testMatch:'website.spec.ts',timeout:process.env.FORGE_SITE_ORIGIN?120000:30000,expect:{timeout:process.env.FORGE_SITE_ORIGIN?20000:5000},use:{baseURL:process.env.FORGE_SITE_ORIGIN??'http://localhost:18103',headless:true},reporter:'line',outputDir:'../frontend/test-results'});
