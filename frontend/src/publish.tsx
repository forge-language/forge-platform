import {useState, type FormEvent} from 'react';
import {useText} from './i18n';
import {api, ApiError, tokenKey, type RegistryUser} from './registry-api';

type Finding = {severity?: string; code?: string; rule?: string; message?: string; path?: string; file?: string};
type Inspection = {
  status?: string; commit?: string; git_commit?: string; source_commit?: string;
  repository_url?: string; manifest_path?: string; inspected_at?: string; checked_at?: string; scanned_files?: number;
  findings?: Finding[]; files?: unknown[]; requires_manual_review?: boolean;
  manual_review_required?: boolean; [key: string]: unknown;
};
type InspectedPackage = {manifest: Record<string, unknown>; inspection: Inspection};

function InspectionReport({result}: {result: InspectedPackage}) {
  const t = useText();
  const {manifest, inspection} = result;
  const commit = inspection.commit ?? inspection.git_commit ?? inspection.source_commit ?? String(manifest.git_commit ?? '');
  return <article className="border-t border-line pt-6 mt-8" aria-label="저장소 검사 보고서">
    <h2 className="text-2xl font-semibold mb-4">{t('Source inspection report', '소스 검사 결과')}</h2>
    <dl className="grid sm:grid-cols-[140px_1fr] gap-3 text-sm">
      <dt className="text-muted">모듈</dt><dd>{String(manifest.name ?? '')} · {String(manifest.version ?? '')}</dd>
      <dt className="text-muted">{t('Pinned commit', '고정 커밋')}</dt><dd className="font-mono break-all">{commit || '보고서에 커밋이 없습니다.'}</dd>
      <dt className="text-muted">{t('Inspection status', '검사 상태')}</dt><dd>{inspection.status ?? '보고서 확인 필요'}</dd>
      {(inspection.inspected_at || inspection.checked_at) && <><dt className="text-muted">{t('Inspected at', '검사 시각')}</dt><dd>{new Date(inspection.inspected_at ?? inspection.checked_at ?? '').toLocaleString()}</dd></>}
    {typeof inspection.scanned_files === 'number' && <><dt className="text-muted">{t('Scanned files', '검사 파일')}</dt><dd>{inspection.scanned_files}개</dd></>}
    </dl>
    <p className="text-sm leading-7 mt-5">{t('Static inspection does not guarantee safety. Review files and dependencies yourself. Native bridges, CMake, JavaScript and external commands can execute code during installation or runtime.', '정적 검사는 안전성을 보증하지 않습니다. 파일과 의존성을 직접 검토하세요. 네이티브 브리지·CMake·JavaScript·외부 명령은 설치 또는 실행 과정에서 코드를 실행할 수 있습니다.')}</p>
    {(inspection.status === 'review_required' || inspection.requires_manual_review || inspection.manual_review_required || Boolean(manifest.native) || Boolean(manifest.javascript)) && <p className="text-sm mt-3">{t('Manual review required: this release includes native or executable code.', '수동 검토 필요: 네이티브 또는 실행 코드를 포함하는 릴리스입니다.')}</p>}
    {inspection.findings && <div className="mt-5"><h3 className="font-semibold mb-3">{t('Findings', '발견 사항')}</h3>{inspection.findings.length ? <ul className="space-y-3 text-sm">{inspection.findings.map((finding, index) => <li key={index}><span className="font-mono">{finding.severity ?? 'info'}{(finding.code || finding.rule) ? ` / ${finding.code ?? finding.rule}` : ''}</span>{' '}{finding.message ?? JSON.stringify(finding)}{(finding.path || finding.file) && <code className="block break-all mt-1">{finding.path ?? finding.file}</code>}</li>)}</ul> : <p className="text-sm text-muted">{t('No findings were detected automatically. This does not establish safety.', '자동 검사에서 발견된 항목이 없습니다. 안전성이 확인되었다는 뜻은 아닙니다.')}</p>}</div>}
    <details className="mt-5 text-sm"><summary className="cursor-pointer link">{t('Scanned files and full report', '검사 파일과 전체 보고서')}</summary><pre className="code mt-3 overflow-x-auto">{JSON.stringify(inspection, null, 2)}</pre></details>
    <details className="mt-4 text-sm"><summary className="cursor-pointer link">{t('Inspected module.json', '확인한 module.json')}</summary><pre className="code mt-3 overflow-x-auto">{JSON.stringify(manifest, null, 2)}</pre></details>
  </article>;
}

export function Publish({user, logout, onLogin}: {user: RegistryUser | null; logout: () => void; onLogin: (user: RegistryUser) => void}) {
  const t = useText();
  const [githubToken, setGithubToken] = useState('');
  const [repository, setRepository] = useState('');
  const [ref, setRef] = useState('');
  const [manifestPath, setManifestPath] = useState('module.json');
  const [inspected, setInspected] = useState<InspectedPackage | null>(null);
  const [confirmed, setConfirmed] = useState(false);
  const [busy, setBusy] = useState('');
  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');
  const [manual, setManual] = useState('');
  const request = {repository_url: repository.trim(), ...(ref.trim() ? {ref: ref.trim()} : {}), manifest_path: manifestPath.trim() || 'module.json'};
  function resetInspection() {setInspected(null); setConfirmed(false); setSuccess(''); setError('');}
  async function login(event: FormEvent) {
    event.preventDefault(); setBusy('login'); setError('');
    const submittedToken = githubToken; setGithubToken('');
    try {
      const response = await api<{token: string; user?: RegistryUser}>('/api/auth/github/token', {method: 'POST', body: JSON.stringify({token: submittedToken})});
      sessionStorage.setItem(tokenKey, response.token);
      onLogin(response.user ?? await api<RegistryUser>('/api/me'));
    } catch (cause) {sessionStorage.removeItem(tokenKey); setError(cause instanceof Error ? cause.message : String(cause));}
    finally {setBusy('');}
  }
  async function inspect(event: FormEvent) {
    event.preventDefault(); setBusy('inspect'); setError(''); setSuccess(''); setInspected(null); setConfirmed(false);
    try {setInspected(await api<InspectedPackage>('/api/packages/inspect', {method: 'POST', body: JSON.stringify(request)}));}
    catch (cause) {
      if (cause instanceof ApiError && cause.body && typeof cause.body === 'object' && 'inspection' in cause.body) {
        const detail = cause.body as {inspection: Inspection; manifest?: Record<string, unknown>};
        setInspected({inspection: detail.inspection, manifest: detail.manifest ?? {repository_url: request.repository_url}});
      }
      setError(cause instanceof Error ? cause.message : String(cause));
    } finally {setBusy('');}
  }
  async function register() {
    if (!inspected || !confirmed || !user) return;
    setBusy('register'); setError(''); setSuccess('');
    try {
      // Registration rechecks the inspected immutable source, not a movable branch.
      const commit = inspected.manifest.git_commit ?? inspected.inspection.commit ?? inspected.inspection.git_commit ?? inspected.inspection.source_commit;
      if (typeof commit !== 'string' || !/^[a-f0-9]{40}$/i.test(commit)) throw new Error('검사 보고서에 유효한 고정 커밋이 없습니다. 다시 검사하세요.');
      await api('/api/packages/register', {method: 'POST', body: JSON.stringify({...request, ref: commit, acknowledge_review: confirmed})});
      setSuccess(t('Release registered. Find it in the module registry.', '릴리스가 등록되었습니다. 모듈 목록에서 확인하세요.')); setConfirmed(false);
    } catch (cause) {setError(cause instanceof Error ? cause.message : String(cause));}
    finally {setBusy('');}
  }
  async function publishManual(event: FormEvent) {
    event.preventDefault(); setBusy('manual'); setError(''); setSuccess('');
    try {await api('/api/packages', {method: 'POST', body: JSON.stringify(JSON.parse(manual))}); setSuccess(t('Release registered. Find it in the module registry.', '릴리스가 등록되었습니다. 모듈 목록에서 확인하세요.'));}
    catch (cause) {setError(cause instanceof Error ? cause.message : String(cause));}
    finally {setBusy('');}
  }
  const blocked = Boolean(error) || ['blocked', 'rejected', 'error', 'failed'].includes(inspected?.inspection.status ?? '');
  return <section className="content max-w-4xl py-14">
    <h1 className="text-4xl font-semibold mb-4">{t('Publish module', '모듈 배포')}</h1>
    <p className="text-muted leading-7 mb-8">{t('Inspect module.json and actual repository files, then register the reviewed commit. Personal modules require repository ownership; forge-language modules require an organization administrator. Existing names and versions cannot be overwritten.', 'GitHub 저장소의 module.json과 실제 파일을 검사하고, 확인한 커밋으로 릴리스를 등록합니다. 저장소 소유자만 자신의 모듈을 등록할 수 있습니다. forge-language 모듈은 조직 관리자가 등록합니다. 기존 이름과 버전은 덮어쓸 수 없습니다.')}</p>
    {!user ? <div className="border-y border-line py-6 mb-8">
      <h2 className="text-xl font-semibold mb-3">{t('Sign in with GitHub', 'GitHub 계정으로 로그인')}</h2>
      <p className="text-sm text-muted leading-7 mb-5">{t('Verify your account using a GitHub personal access token. Public repository lookup requires read access only. The token is sent once for sign-in and the input is cleared immediately. Only the website session is stored in this browser.', 'GitHub 개인 액세스 토큰으로 계정을 확인합니다. 공개 저장소 조회에는 읽기 권한만 필요합니다. 토큰은 로그인 요청 한 번에만 사용하며 입력란을 즉시 비웁니다. 이 브라우저에는 사이트 로그인 세션만 저장합니다.')}</p>
      <form onSubmit={event => void login(event)} className="flex flex-col sm:flex-row gap-3 items-end">
        <label className="flex-1 w-full text-sm">{t('GitHub personal access token', 'GitHub 개인 액세스 토큰')}<input type="password" value={githubToken} onChange={event => setGithubToken(event.target.value)} autoComplete="off" spellCheck={false} required className="mt-2"/></label>
        <button className="button" disabled={Boolean(busy)}>{busy === 'login' ? t('Verifying account…', '계정 확인 중…') : t('Sign in with token', '토큰으로 로그인')}</button>
      </form>
      <details className="mt-5 text-sm"><summary className="cursor-pointer link">{t('When an OAuth app is configured', 'OAuth 앱이 설정된 경우')}</summary><a className="link inline-block mt-3" href="/api/auth/github/login?state=%2Fpublish">{t('Sign in with GitHub OAuth ↗', 'GitHub OAuth로 로그인 ↗')}</a></details>
    </div> : <div className="flex justify-between border-y border-line py-4 mb-8 text-sm"><span>{user.sub} · {user.role}</span><button onClick={() => {logout(); setSuccess('');}} className="link">{t('Sign out', '로그아웃')}</button></div>}
    <form onSubmit={event => void inspect(event)} className="space-y-5">
      <label className="block text-sm">{t('GitHub repository URL', 'GitHub 저장소 URL')}<input type="url" disabled={Boolean(busy)} value={repository} onChange={event => {setRepository(event.target.value); resetInspection();}} placeholder="https://github.com/owner/my-module" required className="mt-2"/></label>
      <div className="grid sm:grid-cols-2 gap-5">
        <label className="block text-sm">{t('Branch, tag or commit (blank uses the default branch)', '브랜치·태그·커밋 (빈칸이면 기본 브랜치)')}<input disabled={Boolean(busy)} value={ref} onChange={event => {setRef(event.target.value); resetInspection();}} placeholder="main 또는 40자리 커밋" className="mt-2"/></label>
        <label className="block text-sm">{t('Manifest path', 'manifest 경로')}<input disabled={Boolean(busy)} value={manifestPath} onChange={event => {setManifestPath(event.target.value); resetInspection();}} placeholder="module.json" className="mt-2"/></label>
      </div>
      <button className="button" disabled={Boolean(busy)}>{busy === 'inspect' ? t('Inspecting repository…', '저장소 검사 중…') : t('Inspect repository', '저장소 검사')}</button>
    </form>
    {inspected && <><InspectionReport result={inspected}/>{!blocked && <div className="mt-6 space-y-4"><label className="flex items-start gap-3 text-sm leading-7"><input type="checkbox" className="mt-2 !w-auto" checked={confirmed} onChange={event => setConfirmed(event.target.checked)} disabled={!user || Boolean(busy)}/>{t('I reviewed the report and pinned commit. I understand the risk of installation and runtime code and want to register this release.', '보고서와 고정 커밋을 검토했습니다. 설치·실행 코드의 위험을 이해하고 등록합니다.')}</label><button className="button" onClick={() => void register()} disabled={!user || !confirmed || Boolean(busy)}>{busy === 'register' ? t('Rechecking and registering…', '다시 검사하고 등록 중…') : t('Register reviewed release', '검토한 릴리스 등록')}</button>{!user && <p className="text-sm text-muted">{t('Sign in with GitHub to register a release.', '등록하려면 GitHub 계정으로 로그인하세요.')}</p>}</div>}</>}
    {error && <p role="alert" className="mt-6 border border-line p-4">{error}</p>}
    {success && <p role="status" className="mt-6 border border-line p-4">{success} <a className="link" href="/packages">{t('Module registry →', '모듈 목록 →')}</a></p>}
    {user && <details className="mt-10 border-t border-line pt-5"><summary className="cursor-pointer link text-sm">{t('Register a manifest manually', '기존 manifest 직접 등록')}</summary><p className="text-sm text-muted leading-7 my-4">{t('The server still verifies the real repository and ownership when you submit JSON directly.', 'JSON을 직접 제출해도 서버가 실제 저장소와 소유권을 다시 확인합니다.')}</p><form onSubmit={event => void publishManual(event)}><label className="block text-sm">{t('Release manifest (JSON)', '릴리스 manifest (JSON)')}<textarea value={manual} onChange={event => setManual(event.target.value)} className="font-mono text-sm leading-6 min-h-64 mt-2" spellCheck={false} required/></label><button className="button mt-4" disabled={Boolean(busy)}>{t('Register manifest', 'manifest 등록')}</button></form></details>}
  </section>;
}
