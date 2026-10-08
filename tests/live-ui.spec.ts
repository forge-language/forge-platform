import {test, expect} from '../frontend/node_modules/@playwright/test/index';
test.beforeEach(async ({page}) => {await page.addInitScript(() => localStorage.setItem('forge_language', 'ko'));});
const commit = 'a'.repeat(40);
const manifest = {schema_version: 1, generated_at: '2026-10-06T00:00:00Z', content_id: 'snapshot',
  repositories: [{name: 'forge-runtime', url: 'https://github.com/forge-language/forge-runtime', description: 'runtime', default_branch: 'main', pushed_at: '2026-10-06T00:00:00Z', stars: 7, forks: 2, open_items: 3, archived: false}],
  documents: [], reports: [{title: 'Live report', path: 'reports/test.md', url: '/live/snapshots/snapshot/reports/test.md', repository: 'forge-benchmarks', commit, sha256: 'b'.repeat(64)}],
  latest_benchmark: {status: 'available', url: '/live/snapshots/snapshot/benchmarks/latest.json'}, errors: [],
};
test('live org data polls, preserves last good result on failed refresh, and links current reports', async ({page}) => {
  await page.clock.install();
  let fail = false;
  let fetches = 0;
  await page.route('**/live/manifest.json', route => {fetches++; return fail ? route.fulfill({status: 503, body: 'unavailable'}) : route.fulfill({json: manifest});});
  await page.route('**/live/benchmarks/latest.json', route => route.fulfill({json: {schema_version: 1, measured_at: '2026-10-06T00:00:00Z', source_revisions: {'forge-runtime': commit}, environment: {os: 'Linux test'}, scope: 'fixture workload', repeats: 3, summary: {scheduler: [{case: 'yield', wall_ms: {median: 1.2, min: 1.1, max: 1.3}, process_cpu_ms: {median: 2.4}}], parser: {parse_seconds: {median: 0.01}, realloc_calls: {median: 4}}}, checks: {scheduler_completed: true}}}));
  await page.goto('/agents');
  await expect(page.getByRole('link', {name: 'forge-runtime ↗', exact: true})).toBeVisible();
  await expect(page.getByText('Stars 7 · Forks 2 · Open issues + PRs 3')).toBeVisible();
  const initial = fetches;
  fail = true;
  await page.clock.fastForward(61_000);
  await expect(page.getByRole('alert')).toContainText('마지막으로 받은 데이터');
  await expect(page.getByRole('link', {name: 'forge-runtime ↗', exact: true})).toBeVisible();
  expect(fetches).toBeGreaterThan(initial);
  fail = false;
  await page.goto('/benchmarks');
  await expect(page.getByRole('cell', {name: '1.2', exact: true})).toBeVisible();
  await expect(page.getByRole('link', {name: 'Live report ↗'})).toHaveAttribute('href', '/live/snapshots/snapshot/reports/test.md');
  await expect(page.getByRole('link', {name: 'forge-benchmarks 측정 소스·재현 명령 ↗'})).toHaveAttribute('href', 'https://github.com/forge-language/forge-benchmarks');
});

test('token login clears PAT, inspection pins SHA, register waits for success and acknowledges review', async ({page}) => {
  let registerBody: unknown;
  await page.route('**/api/auth/github/token', route => {
    expect(route.request().postDataJSON()).toEqual({token: 'fixture-not-a-real-pat'});
    return route.fulfill({json: {token: 'site-session-jwt', user: {sub: 'fixture-owner', role: 'user'}}});
  });
  await page.route('**/api/packages/inspect', route => route.fulfill({json: {manifest: {name: 'fixture-module', version: '0.1.0', git_commit: commit, native: {library: 'x'}}, inspection: {status: 'review_required', git_commit: commit, scanned_files: 4, findings: [{severity: 'warning', rule: 'native-code', path: 'CMakeLists.txt', message: 'Native code requires review'}], checked_at: '2026-10-06T00:00:00Z'}}}));
  await page.route('**/api/packages/register', async route => {
    registerBody = route.request().postDataJSON();
    await route.fulfill({json: {name: 'fixture-module', version: '0.1.0'}});
  });
  await page.goto('/publish');
  await page.getByLabel('GitHub 개인 액세스 토큰', {exact: true}).fill('fixture-not-a-real-pat');
  await page.getByRole('button', {name: '토큰으로 로그인'}).click();
  await expect(page.getByText('fixture-owner · user')).toBeVisible();
  expect(await page.evaluate(() => JSON.stringify({session: {...sessionStorage}, local: {...localStorage}}))).not.toContain('fixture-not-a-real-pat');
  expect(await page.evaluate(() => sessionStorage.getItem('forge_registry_token'))).toBe('site-session-jwt');
  await page.getByLabel('GitHub 저장소 URL').fill('https://github.com/fixture-owner/fixture-module');
  await page.getByRole('button', {name: '저장소 검사', exact: true}).click();
  await expect(page.getByText(commit, {exact: true})).toBeVisible();
  await expect(page.getByRole('listitem').filter({hasText: 'Native code requires review'})).toBeVisible();
  const register = page.getByRole('button', {name: '검토한 릴리스 등록'});
  await expect(register).toBeDisabled();
  await page.getByRole('checkbox').check();
  await register.click();
  await expect(page.getByRole('status')).toContainText('릴리스가 등록되었습니다');
  expect(registerBody).toEqual({repository_url: 'https://github.com/fixture-owner/fixture-module', manifest_path: 'module.json', ref: commit, acknowledge_review: true});
});

test('blocked repository errors retain findings and cannot be registered', async ({page}) => {
  await page.route('**/api/packages/inspect', route => route.fulfill({status: 422, json: {error: 'repository_rejected', message: 'Source rejected', inspection: {status: 'blocked', git_commit: commit, findings: [{severity: 'high', rule: 'private-key', path: 'secret.pem', message: 'Private key detected'}]}}}));
  await page.goto('/publish');
  await page.getByLabel('GitHub 저장소 URL').fill('https://github.com/fixture-owner/bad-module');
  await page.getByRole('button', {name: '저장소 검사', exact: true}).click();
  await expect(page.getByRole('alert')).toContainText('Source rejected');
  await expect(page.getByRole('listitem').filter({hasText: 'Private key detected'})).toBeVisible();
  await expect(page.getByRole('button', {name: '검토한 릴리스 등록'})).toHaveCount(0);
  await expect(page.getByRole('status')).toHaveCount(0);
});

test('registration authorization failure never reports a published release', async ({page}) => {
  await page.addInitScript(() => sessionStorage.setItem('forge_registry_token', 'fixture-session'));
  await page.route('**/api/me', route => route.fulfill({json: {sub: 'fixture-owner', role: 'user'}}));
  await page.route('**/api/packages/inspect', route => route.fulfill({json: {manifest: {name: 'fixture-module', version: '0.1.0', git_commit: commit}, inspection: {status: 'passed', git_commit: commit, findings: []}}}));
  await page.route('**/api/packages/register', route => route.fulfill({status: 403, json: {error: 'not_repository_owner', message: 'Repository ownership required'}}));
  await page.goto('/publish');
  await expect(page.getByText('fixture-owner · user')).toBeVisible();
  await page.getByLabel('GitHub 저장소 URL').fill('https://github.com/another-owner/fixture-module');
  await page.getByRole('button', {name: '저장소 검사', exact: true}).click();
  await page.getByRole('checkbox').check();
  await page.getByRole('button', {name: '검토한 릴리스 등록'}).click();
  await expect(page.getByRole('alert')).toContainText('Repository ownership required');
  await expect(page.getByRole('status')).toHaveCount(0);
});

test('null and non-JSON API failures remain actionable without leaking token input', async ({page}) => {
  let mode = 'null';
  await page.route('**/api/auth/github/token', route => mode === 'null' ? route.fulfill({status: 503, json: null}) : route.fulfill({status: 502, contentType: 'text/plain', body: 'upstream unavailable'}));
  await page.goto('/publish');
  const token = page.getByLabel('GitHub 개인 액세스 토큰', {exact: true});
  await token.fill('fixture-not-a-real-pat');
  await page.getByRole('button', {name: '토큰으로 로그인'}).click();
  await expect(page.getByRole('alert')).toHaveText('HTTP 503');
  await expect(token).toHaveValue('');
  mode = 'text';
  await token.fill('fixture-not-a-real-pat');
  await page.getByRole('button', {name: '토큰으로 로그인'}).click();
  await expect(page.getByRole('alert')).toContainText('HTTP 502');
  await expect(page.getByRole('alert')).toContainText('JSON');
  await expect(token).toHaveValue('');
});

test('merged language switch, syntax page and immutable document viewer remain usable', async ({page}) => {
  const snapshot = 'c'.repeat(24);
  const documentPath = `/live/snapshots/${snapshot}/documents/ARCHITECTURE.md`;
  await page.route('**/live/manifest.json', route => route.fulfill({json: {...manifest, documents: [{title: 'Architecture', path: 'documents/ARCHITECTURE.md', url: documentPath, repository: 'forge', commit, sha256: 'd'.repeat(64)}]}}));
  await page.route('**/benchmarks.json', route => route.fulfill({json: {updated_at: null, checked_at: '2026-10-06T00:00:00Z', reports: [], errors: []}}));
  await page.route(`**${documentPath}`, route => route.fulfill({contentType: 'text/plain', body: '# Fixture Architecture\n\nPinned source document.'}));
  await page.goto('/publish');
  await page.getByLabel('Language / 언어').selectOption('en');
  await expect(page.getByRole('heading', {level: 1})).toHaveText('Publish module');
  await expect(page.getByLabel('GitHub personal access token', {exact: true})).toHaveAttribute('type', 'password');
  await page.goto('/docs/syntax');
  await page.getByLabel('Language / 언어').selectOption('en');
  await expect(page.getByRole('heading', {name: 'Print text and values', exact: true})).toBeVisible();
  await page.goto(`/view?file=${encodeURIComponent(documentPath)}`);
  await expect(page.getByRole('heading', {name: 'Fixture Architecture', exact: true})).toBeVisible();
  await page.goto('/view?file=%2Flive%2Fsnapshots%2F'+snapshot+'%2Fdocuments%2F..%2Fsecret.md');
  await page.getByLabel('Language / 언어').selectOption('en');
  await expect(page.getByRole('alert')).toContainText('path is invalid');
});

test('package detail shows only matching stored inspection and warns on legacy or forged records', async ({page}) => {
  const repository = 'https://github.com/fixture-owner/fixture-module';
  const release = {name: 'fixture-module', version: '1.2.0', description: 'Fixture', repository_url: repository, git_commit: commit, module: 'module.fg', license: 'Apache-2.0', dependencies: {}, native: {library: 'fixture', cmake_target: 'fixture', pkg_config: []}};
  const report = {policy: 'forge-source-inspection-v1', repository, git_commit: commit, status: 'review_required', checked_at: '2026-10-07T00:00:00Z', scanned_files: 3, findings: [{severity: 'warning', rule: 'native-code', path: 'CMakeLists.txt', message: 'Fixture native review finding'}]};
  await page.route('**/api/packages/fixture-module', route => route.fulfill({json: {name: release.name, owner: 'fixture-owner', versions: [{...release, security: report}, {...release, version: '1.1.0'}, {...release, version: '1.0.0', security: {...report, git_commit: 'b'.repeat(40), status: 'passed'}}]}}));
  await page.goto('/packages/fixture-module');
  const panel = page.getByRole('region', {name: '등록 당시 검사'});
  await expect(panel).toContainText('소스 수동 검토 필요');
  await expect(panel.getByRole('listitem')).toContainText('Fixture native review finding');
  await expect(panel).toContainText('모든 악성 코드나 실행 동작을 탐지했다는 보장이 아닙니다');
  await page.getByLabel('버전', {exact: true}).selectOption('1');
  await expect(panel).toContainText('유효한 등록 당시 검사 기록이 없습니다');
  await expect(panel).not.toContainText('정적 검사 완료');
  await page.getByLabel('버전', {exact: true}).selectOption('2');
  await expect(panel).toContainText('유효한 등록 당시 검사 기록이 없습니다');
  await expect(panel).not.toContainText('정적 검사 완료');
  await page.getByLabel('Language / 언어').selectOption('en');
  await expect(page.getByRole('region', {name: 'Inspection recorded at registration'})).toContainText('No valid registration inspection');
});
