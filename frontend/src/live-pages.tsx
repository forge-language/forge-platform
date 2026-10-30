import {Title} from './home';
import {useText} from './i18n';
import {useLiveJson, LiveStatus, SourceLinks, timestamp, type LiveManifest, type BenchmarkReport} from './public-live-data';
const benchmarkRepository = 'https://github.com/forge-language/forge-benchmarks';

export function Benchmarks() {
  const t = useText();
  const manifest = useLiveJson<LiveManifest>('/live/manifest.json');
  const latest = useLiveJson<BenchmarkReport>('/live/benchmarks/latest.json');
  const report = latest.data;
  return <section className="content py-16">
    <Title label="PROOF / REPRODUCIBLE MEASUREMENTS" title={t('Measurements with their source.', '측정 조건부터 공개합니다.')}>{t('We publish measured results and raw observations. These Forge workloads ran on the same host; they do not establish an advantage over other languages.', '실제로 측정한 결과와 원시 데이터를 공개합니다. 현재 측정은 동일 호스트에서 실행한 Forge 작업이며 다른 언어보다 빠르다는 근거로 사용하지 않습니다.')}</Title>
    <LiveStatus generatedAt={manifest.data?.generated_at} checkedAt={manifest.checkedAt} error={manifest.error}/>
    {manifest.data?.errors?.map(error => <p className="text-sm text-muted mb-3" key={error.component}>{error.component} 갱신 상태: {error.message}</p>)}
    <article className="border-t border-ink pt-7 mb-12">
      <h2 className="text-2xl font-semibold mb-4">{t('Latest measured run', '최근 실행한 측정')}</h2>
      <LiveStatus checkedAt={latest.checkedAt} error={latest.error}/>
      {!report ? <p role="status" className="text-muted">{manifest.data?.latest_benchmark?.status === 'unavailable' ? '아직 공개된 최신 측정이 없습니다.' : '측정 데이터를 불러오는 중…'}</p> : <>
        <p className="text-sm text-muted mb-4">측정: {timestamp(report.measured_at)} · 반복: {report.repeats}회</p>
        <p className="text-sm leading-7 mb-6">{report.scope}</p>
        {report.summary.scheduler?.length ? <div className="overflow-x-auto mb-7"><h3 className="text-lg font-semibold mb-4">{t('Scheduler', '스케줄러')}</h3><table className="w-full text-sm border-y border-line"><thead><tr className="text-left"><th className="py-3 pr-5">{t('Workload', '작업')}</th><th className="py-3 pr-5">{t('Median wall time (ms)', 'Wall 중앙값 (ms)')}</th><th className="py-3 pr-5">{t('Min–max (ms)', '최소–최대 (ms)')}</th><th className="py-3">{t('Median process CPU (ms)', 'Process CPU 중앙값 (ms)')}</th></tr></thead><tbody>{report.summary.scheduler.map(item => <tr className="border-t border-line" key={item.case}><th className="text-left font-mono font-normal py-3 pr-5">{item.case}</th><td className="py-3 pr-5">{item.wall_ms.median ?? '—'}</td><td className="py-3 pr-5">{item.wall_ms.min ?? '—'}–{item.wall_ms.max ?? '—'}</td><td className="py-3">{item.process_cpu_ms.median ?? '—'}</td></tr>)}</tbody></table></div> : null}
        {report.summary.parser && <div className="mb-7"><h3 className="text-lg font-semibold mb-3">{t('Parser', '파서')}</h3><p className="text-sm leading-7">파싱 시간 중앙값: {report.summary.parser.parse_seconds?.median ?? '—'}초 · realloc 호출 중앙값: {report.summary.parser.realloc_calls?.median ?? '—'}</p></div>}
        {report.summary.strings && <details className="mb-6 text-sm"><summary className="cursor-pointer link">{t('String measurements', '문자열 측정 결과')}</summary><pre className="code mt-4 overflow-x-auto">{JSON.stringify(report.summary.strings, null, 2)}</pre></details>}
        <details className="text-sm mb-6"><summary className="cursor-pointer link">{t('Environment, source commits and workload checks', '실행 환경·소스 커밋·검증 조건')}</summary><pre className="code mt-4 overflow-x-auto">{JSON.stringify({environment: report.environment, source_revisions: report.source_revisions, checks: report.checks}, null, 2)}</pre></details>
        <div className="flex gap-5 flex-wrap text-sm"><a className="link" href={manifest.data?.latest_benchmark?.url ?? '/live/benchmarks/latest.json'}>{t('Complete raw JSON', '전체 원시 JSON')}</a>{(report.run_url || manifest.data?.latest_benchmark?.run_url) && <a className="link" href={report.run_url ?? manifest.data?.latest_benchmark?.run_url}>{t('Execution and raw files ↗', '실행 결과·원시 파일 ↗')}</a>}</div>
      </>}
      <a className="link inline-block text-sm mt-6" href={benchmarkRepository}>{t('forge-benchmarks source and reproduction commands ↗', 'forge-benchmarks 측정 소스·재현 명령 ↗')}</a>
    </article>
    <h2 className="text-2xl font-semibold mb-5">{t('Development and performance reports', '개발·성능 보고서')}</h2>
    {manifest.data ? <SourceLinks documents={manifest.data.reports ?? []}/> : <p role="status">{t('Loading report list…', '보고서 목록을 불러오는 중…')}</p>}
    {manifest.data && !manifest.data.reports?.length && <p className="text-muted">{t('No reports are published yet.', '공개된 보고서가 없습니다.')}</p>}
    <h2 className="text-2xl font-semibold mt-10 mb-5">{t('Current specifications and reproduction documents', '현재 명세와 재현 문서')}</h2>
    {manifest.data && <SourceLinks documents={manifest.data.documents ?? []}/>}
  </section>;
}

export function Agents() {
  const t = useText();
  const {data, error, checkedAt} = useLiveJson<LiveManifest>('/live/manifest.json');
  return <section className="content py-16">
    <Title label="WATCH / PUBLIC DEVELOPMENT" title="AI builds. Humans decide.">{t('Browse real repositories and public pull requests from forge-language. We do not infer agent participation from author names or commits.', 'forge-language 조직의 실제 저장소와 공개 PR을 확인하세요. 작성자나 커밋만으로 에이전트 참여 여부를 추정하지 않습니다.')}</Title>
    <LiveStatus generatedAt={data?.generated_at} checkedAt={checkedAt} error={error}/>
    {data?.errors?.map(item => <p className="text-sm text-muted mb-3" key={item.component}>{item.component}: {item.message}</p>)}
    {!data ? <p role="status">{t('Loading organization repositories…', '조직 저장소를 불러오는 중…')}</p> : <>
      <h2 className="text-2xl font-semibold mb-5">{t('Organization repositories', '조직 저장소')}</h2>
      <div className="border-t border-ink">{data.repositories.map(repository => <article key={repository.name} className="border-b border-line py-6">
        <div className="flex flex-wrap justify-between gap-4"><h3 className="text-xl font-mono font-semibold"><a className="link" href={repository.url}>{repository.name} ↗</a></h3><span className="text-sm text-muted">{repository.archived ? '보관된 저장소' : '공개 저장소'} · 기본 브랜치 {repository.default_branch}</span></div>
        {repository.description && <p className="text-muted leading-7 mt-3">{repository.description}</p>}
        <p className="text-xs font-mono text-muted mt-4">Stars {repository.stars} · Forks {repository.forks} · Open issues + PRs {repository.open_items}</p>
        <p className="text-sm text-muted mt-2">최근 푸시: {timestamp(repository.pushed_at)} · <a className="link" href={`${repository.url}/actions`}>CI 기록</a> · <a className="link" href={`${repository.url}/issues`}>이슈</a></p>
      </article>)}</div>
      {data.activity && <div className="mt-12"><h2 className="text-2xl font-semibold mb-3">{t('Recent pull requests', '최근 PR')}</h2><p className="text-sm text-muted mb-5">{data.activity.repository} · 조회 {timestamp(data.activity.updated_at)}</p><div className="border-t border-line">{data.activity.pulls?.length ? data.activity.pulls.map(pr => <a className="block border-b border-line py-5" key={pr.url} href={pr.url}><p className="font-mono text-xs text-muted mb-2">#{pr.number} · {pr.merged_at ? 'Merged' : pr.state === 'open' ? 'Open / GitHub에서 리뷰 상태 확인' : 'Closed'} · @{pr.author}</p><h3 className="text-xl font-semibold">{pr.title} ↗</h3>{pr.labels?.length > 0 && <p className="text-sm text-muted mt-3">{pr.labels.join(' · ')}</p>}</a>) : <p className="py-5 text-muted">이 스냅샷에 공개 PR이 없습니다.</p>}</div></div>}
    </>}
    <p className="text-sm text-muted leading-7 mt-8">{t('These are GitHub API snapshots. Running agent states and unverified contribution totals are not displayed.', 'GitHub API 스냅샷입니다. 실행 중인 에이전트 상태나 검증되지 않은 기여 수는 표시하지 않습니다.')}</p>
    <a className="link inline-block mt-5" href="https://github.com/orgs/forge-language/repositories">{t('Current organization repositories on GitHub ↗', 'GitHub 조직의 최신 저장소 목록 ↗')}</a>
  </section>;
}

export function Blog() {
  const t = useText();
  const {data, error, checkedAt} = useLiveJson<LiveManifest>('/live/manifest.json');
  return <section className="content py-16"><Title label="NOTES / DEVELOPMENT JOURNAL" title={t('The process is part of the project.', '과정이 프로젝트의 일부입니다.')}>{t('Read real development and performance reports synchronized from repositories, with each file’s source and commit recorded.', '저장소에서 동기화한 실제 개발·성능 보고서를 공개합니다. 각 파일의 출처와 커밋을 함께 기록합니다.')}</Title><LiveStatus generatedAt={data?.generated_at} checkedAt={checkedAt} error={error}/>{data ? <SourceLinks documents={data.reports ?? []}/> : <p role="status">{t('Loading development records…', '개발 기록을 불러오는 중…')}</p>}</section>;
}
