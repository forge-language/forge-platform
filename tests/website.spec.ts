import {test,expect} from '../frontend/node_modules/@playwright/test/index';
test('installation docs, real registry search and version detail',async({page})=>{
 const errors:string[]=[];page.on('pageerror',e=>errors.push(e.message));
 const logoRequest=page.waitForResponse(response=>response.url().endsWith('/logo_named.webp')&&response.ok());await page.goto('/');await logoRequest;
 await expect(page.getByRole('img',{name:'Forge'})).toBeVisible();await expect(page.locator('link[rel="icon"]')).toHaveAttribute('href','/logo.webp');
 await expect(page.getByRole('heading',{level:1})).toContainText('C로 컴파일하는 Forge');
 await page.getByRole('link',{name:'설치하고 시작하기'}).click();await expect(page.getByText('curl -fsSL',{exact:false})).toBeVisible();
 await page.getByRole('link',{name:'모듈',exact:true}).click();await page.getByRole('searchbox').fill('postgres');
 await page.getByRole('heading',{name:'forge-postgres',exact:true}).click();await expect(page.getByRole('heading',{level:1})).toHaveText('forge-postgres');
 await expect(page.locator('code').filter({hasText:'forge pkg add forge-postgres 0.1.1'})).toBeVisible();
 await page.reload();await expect(page.getByRole('heading',{level:1})).toHaveText('forge-postgres');expect(errors).toEqual([]);
});
test('mobile navigation and anonymous publishing state',async({page})=>{
 await page.setViewportSize({width:390,height:844});await page.goto('/publish');await expect(page.getByRole('link',{name:'GitHub로 로그인'})).toBeVisible();
 expect(await page.evaluate(()=>document.documentElement.scrollWidth)).toBeLessThanOrEqual(390);
});
