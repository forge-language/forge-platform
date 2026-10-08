import {useText} from './i18n';

type ReleaseSource = {repository_url: string; git_commit: string; security?: unknown; native?: unknown; javascript?: unknown};
type Finding = {severity?: unknown; rule?: unknown; path?: unknown; message?: unknown};
type Report = {policy: string; repository: string; git_commit: string; status: 'passed' | 'review_required' | 'blocked'; checked_at?: unknown; scanned_files?: unknown; findings?: unknown};
function matchingReport(release: ReleaseSource): Report | undefined {
  const report = release.security;
  if (!report || typeof report !== 'object' || Array.isArray(report)) return undefined;
  const value = report as Record<string, unknown>;
  if (value.policy !== 'forge-source-inspection-v1' || value.repository !== release.repository_url || value.git_commit !== release.git_commit || !/^[a-f0-9]{40}$/i.test(release.git_commit) || typeof value.status !== 'string' || !['passed', 'review_required', 'blocked'].includes(value.status)) return undefined;
  return value as Report;
}
function text(value: unknown): string {return typeof value === 'string' ? value : '';}
export function StoredInspection({release}: {release: ReleaseSource}) {
  const t = useText();
  const report = matchingReport(release);
  const checkedAt = typeof report?.checked_at === 'string' && Number.isFinite(Date.parse(report.checked_at)) ? new Date(report.checked_at).toLocaleString() : undefined;
  const findings = Array.isArray(report?.findings) ? report.findings.filter((finding): finding is Finding => Boolean(finding) && typeof finding === 'object' && !Array.isArray(finding)) : [];
  return <section aria-label={t('Inspection recorded at registration', '등록 당시 검사')} className="border-t border-line mt-10 pt-6">
    <h2 className="text-2xl font-semibold mb-4">{t('Inspection recorded at registration', '등록 당시 검사')}</h2>
    {report ? <>
      <p className="text-sm leading-7">{report.status === 'passed' ? t('Static inspection completed', '정적 검사 완료') : report.status === 'review_required' ? t('Manual source review required', '소스 수동 검토 필요') : t('Inspection blocked this source', '검사에서 차단된 소스')} · <code>{report.policy}</code></p>
      {checkedAt && <p className="text-sm text-muted mt-2">{t('Inspected at: ', '검사 시각: ')}{checkedAt}</p>}
      {typeof report.scanned_files === 'number' && Number.isInteger(report.scanned_files) && report.scanned_files >= 0 && <p className="text-sm text-muted mt-2">{t('Scanned files: ', '검사 파일: ')}{report.scanned_files}</p>}
      <p className="font-mono break-all text-xs text-muted mt-3">{report.repository} · {report.git_commit}</p>
      {findings.length > 0 && <ul className="text-sm leading-7 mt-4 space-y-3">{findings.map((finding, index) => <li key={index}><span className="font-mono">{text(finding.severity)}{text(finding.rule) ? ` / ${text(finding.rule)}` : ''}</span>{' '}{text(finding.message)}{text(finding.path) && <code className="block break-all">{text(finding.path)}</code>}</li>)}</ul>}
      <details className="text-sm mt-4"><summary className="link cursor-pointer">{t('Full stored inspection report', '저장된 검사 보고서 전체')}</summary><pre className="code overflow-x-auto mt-3">{JSON.stringify(report, null, 2)}</pre></details>
    </> : <p className="text-sm leading-7">{t('No valid registration inspection is recorded for this repository and commit. Review this release and its source manually before use.', '이 저장소와 커밋에 일치하는 유효한 등록 당시 검사 기록이 없습니다. 사용 전에 릴리스와 소스를 직접 검토하세요.')}</p>}
    <p className="text-sm text-muted leading-7 mt-4">{t('This is a static source inspection record, not a guarantee that all malware or runtime behavior was detected. Review dependencies and installation code yourself.', '이 기록은 정적 소스 검사 결과이며 모든 악성 코드나 실행 동작을 탐지했다는 보장이 아닙니다. 의존성과 설치 코드를 직접 검토하세요.')}</p>
    {(Boolean(release.native) || Boolean(release.javascript)) && <p className="text-sm text-muted leading-7 mt-3">{t('Native bridges, CMake and JavaScript may execute code during installation or runtime. Manual review remains necessary.', '네이티브 브리지·CMake·JavaScript는 설치 또는 실행 중 코드를 실행할 수 있으므로 수동 검토가 필요합니다.')}</p>}
  </section>;
}
