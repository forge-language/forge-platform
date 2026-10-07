import {test,expect} from '../frontend/node_modules/@playwright/test/index';
test('installation docs, real registry search and version detail',async({page})=>{
 const errors:string[]=[];page.on('pageerror',e=>errors.push(e.message));
 const logoRequest=page.waitForResponse(response=>response.url().endsWith('/logo_named.webp')&&response.ok());await page.goto('/');await logoRequest;
 await expect(page.getByRole('img',{name:'Forge'})).toBeVisible();await expect(page.locator('link[rel="icon"]')).toHaveAttribute('href','/logo.webp');
 await expect(page.getByRole('heading',{level:1})).toContainText('Safe ownership.');
 await page.getByRole('link',{name:'Documentation',exact:true}).click();await expect(page.getByText('curl -fsSL',{exact:false})).toBeVisible();
 await page.getByRole('link',{name:'Modules',exact:true}).click();await page.getByRole('searchbox').fill('postgres');
 await page.getByRole('heading',{name:'forge-postgres',exact:true}).click();await expect(page.getByRole('heading',{level:1})).toHaveText('forge-postgres');
 await expect(page.locator('code').filter({hasText:'forge pkg add forge-postgres 0.1.1'})).toBeVisible();
 await page.reload();await expect(page.getByRole('heading',{level:1})).toHaveText('forge-postgres');expect(errors).toEqual([]);
});
test('playground compiles edited source, recursion, UTF-8 and exact int64 values',async({page})=>{
 await page.goto('/play');const run=page.getByRole('button',{name:'Run →'});await expect(run).toBeEnabled();
 for (const [name,output] of [['Hello Forge','Hello, Forge!'],['Functions','144'],['UTF-8 strings','안녕하세요, Forge!'],['64-bit integers','9007199254740995']]) {
  await page.getByLabel('예제',{exact:true}).selectOption(name);await run.click();await expect(page.getByRole('status')).toContainText(output);await expect(run).toBeEnabled();
 }
 await page.getByLabel('MAIN.FG').fill('native main { println(6 * 7); return 0; }');await run.click();await expect(page.getByRole('status')).toHaveText('42\n');
});
test('playground reports syntax and native concurrency errors and stops infinite loops',async({page})=>{
 await page.goto('/play');const run=page.getByRole('button',{name:'Run →'});await expect(run).toBeEnabled();
 await page.getByLabel('MAIN.FG').fill('native main { let = ; }');await run.click();await expect(page.getByRole('status')).toContainText('forge:');await expect(run).toBeEnabled();
 await page.getByLabel('MAIN.FG').fill('process main { println("native"); }');await run.click();await expect(page.getByRole('status')).toContainText('JavaScript backend');await expect(run).toBeEnabled();
 await page.getByLabel('MAIN.FG').fill('native main { while (1) { } return 0; }');await run.click();await expect(page.getByRole('status')).toContainText('2초');await expect(run).toBeEnabled();
 await page.getByLabel('MAIN.FG').fill('native main { println("recovered"); return 0; }');await run.click();await expect(page.getByRole('status')).toContainText('recovered');
});
test('sandbox execution cannot access the website token or make network requests',async({page})=>{
 await page.goto('/play');await expect(page.getByRole('button',{name:'Run →'})).toBeEnabled();
 await page.evaluate(()=>sessionStorage.setItem('sandbox-verification','private-test-value'));
 const result=await page.evaluate(()=>new Promise<{output:string,error?:string}>(resolve=>{
  const frame=document.querySelector('iframe')!;
  window.addEventListener('message',function listener(event){if(event.source===frame.contentWindow&&event.data.type==='result'){window.removeEventListener('message',listener);resolve(event.data)}});
  frame.contentWindow!.postMessage({type:'run',javascript:'console.log(typeof sessionStorage); fetch("/api/health").then(()=>console.log("unexpected")).catch(()=>console.log("network blocked"));'},'*');
 }));
 expect(result.output).toContain('undefined');expect(result.output).not.toContain('private-test-value');expect(result.output).not.toContain('unexpected');
});
test('strategy pages and published evidence work on mobile without overflow',async({page,request})=>{
 await page.setViewportSize({width:390,height:844});
 for(const path of ['/','/language','/play','/benchmarks','/agents','/contribute','/community','/blog','/roadmap']) {
  await page.goto(path);await expect(page.getByRole('heading',{level:1})).toBeVisible();
  expect(await page.evaluate(()=>document.documentElement.scrollWidth),path).toBeLessThanOrEqual(390);
 }
 const activity=await request.get('/activity.json');expect(activity.ok()).toBeTruthy();const activityData=await activity.json();expect(activityData.repository).toBe('https://github.com/forge-language/forge');if(activityData.status==='unavailable'){expect(activityData.stats).toBeNull();expect(activityData.updated_at).toBeNull();expect(activityData.pulls).toEqual([])}else{expect(activityData.stats.stars).toBeGreaterThanOrEqual(0);expect(activityData.updated_at).toBeTruthy()};
 for(const path of ['/project/ARCHITECTURE.md','/project/LANGUAGE_SPEC.md','/project/ROADMAP.md','/reports/selfhosting-performance-2026-10-05.md','/reports/selfhosting-performance-2026-10-05-current-input.json'])expect((await request.get(path)).ok(),path).toBeTruthy();
});
test('mobile navigation and anonymous publishing state',async({page})=>{
 await page.setViewportSize({width:390,height:844});await page.goto('/publish');await page.getByLabel('Language / 언어').selectOption('ko');const token=page.getByLabel('GitHub 개인 액세스 토큰',{exact:true});await expect(token).toBeVisible();await expect(token).toHaveAttribute('type','password');
 expect(await page.evaluate(()=>document.documentElement.scrollWidth)).toBeLessThanOrEqual(390);
});
