import {test,expect} from '../frontend/node_modules/@playwright/test/index';
import fs from 'node:fs';
import path from 'node:path';
const course=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../frontend/public/learn/course.json'),'utf8'));
test('English-first course runs every lesson in the actual WASM compiler and sandbox',async({page})=>{
 await page.goto('/learn');
 await expect(page.getByRole('heading',{name:'Learn Forge',exact:true})).toBeVisible();
 await expect(page.locator('html')).toHaveAttribute('lang','en');
 for(const lesson of course.lessons){
  await page.getByRole('button',{name:new RegExp(lesson.title.en.replace(/[.*+?^${}()|[\]\\]/g,'\\$&'))}).click();
  await expect(page.getByLabel('Forge source')).toHaveValue(lesson.source);
  const run=page.getByRole('button',{name:'Run & check',exact:true});await expect(run).toBeEnabled();await run.click();
  await expect(page.getByText('Output matches the example.',{exact:true})).toBeVisible();
  await expect(page.getByRole('status',{name:'Program output'})).toHaveText(lesson.expected_output.trim());
 }
});
test('failed compilation and wrong output are actionable, reset and Korean support work',async({page})=>{
 await page.goto('/learn?lesson=hello');const source=page.getByLabel('Forge source');const run=page.getByRole('button',{name:'Run & check',exact:true});await expect(run).toBeEnabled();
 await source.fill('native main { println("changed"); return 0; }');await run.click();await expect(page.getByText('Output differs from the example. Compare the results above.',{exact:true})).toBeVisible();
 await source.fill('native main { let x: int = ; }');await run.click();await expect(page.getByRole('status',{name:'Program output'})).toContainText(/error|Error|Expected|expected/);await expect(run).toBeEnabled();
 await page.getByRole('button',{name:'Reset example',exact:true}).click();await run.click();await expect(page.getByText('Output matches the example.',{exact:true})).toBeVisible();
 await page.getByLabel('Language / 언어').selectOption('ko');await expect(page.getByRole('heading',{name:'Learn Forge',exact:true})).toBeVisible();await expect(page.getByRole('button',{name:'실행 후 출력 확인',exact:true})).toBeVisible();
});
test('deep links, source download and bounded execution remain usable on mobile',async({page})=>{
 await page.setViewportSize({width:390,height:844});await page.goto('/play?lesson=int64');const run=page.getByRole('button',{name:'Run & check',exact:true});await expect(run).toBeEnabled();await run.click();await expect(page.getByText('Output matches the example.',{exact:true})).toBeVisible();
 const download=page.waitForEvent('download');await page.getByRole('button',{name:'Download .fg',exact:true}).click();expect((await download).suggestedFilename()).toBe('int64.fg');
 await page.getByLabel('Forge source').fill('native main { while (true) {} return 0; }');await run.click();await expect(page.getByRole('status',{name:'Program output'})).toContainText('2-second limit');await expect(run).toBeEnabled();
 const widths=await page.evaluate(()=>[document.documentElement.scrollWidth,innerWidth]);expect(widths[0]).toBeLessThanOrEqual(widths[1]);
});
