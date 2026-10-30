import {useEffect, useState} from 'react';
import {viewerLink} from './live-data';

type Distribution = {median?: number; min?: number; max?: number};
export type Repository = {name: string; url: string; description: string | null; default_branch: string; pushed_at: string | null; stars: number; forks: number; open_items: number; archived: boolean};
export type SourceDocument = {title: string; date?: string; path: string; url: string; repository: string; commit: string; sha256: string};
export type PullRequest = {number: number; title: string; url: string; state: string; merged_at: string | null; author: string; labels: string[]};
export type LiveManifest = {
  schema_version: number; generated_at: string; content_id: string; repositories: Repository[];
  activity?: {updated_at: string; repository: string; stats: {stars: number; forks: number; open_items: number}; pulls: PullRequest[]};
  documents: SourceDocument[]; reports: SourceDocument[];
  latest_benchmark: {status: string; measured_at?: string; url?: string; run_url?: string; source_revisions?: Record<string, string>; summary?: Record<string, unknown>};
  errors?: {component: string; message: string}[];
};
export type BenchmarkReport = {
  schema_version: number; measured_at: string; source_revisions: Record<string, string>;
  environment: Record<string, unknown>; run_url?: string; scope: string; repeats: number;
  summary: {scheduler?: {case: string; wall_ms: Distribution; process_cpu_ms: Distribution}[]; strings?: Record<string, unknown>[] | Record<string, unknown>; parser?: {parse_seconds?: Distribution; realloc_calls?: Distribution}};
  checks?: Record<string, unknown>;
};
export function useLiveJson<T>(url: string) {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState('');
  const [checkedAt, setCheckedAt] = useState<string | null>(null);
  useEffect(() => {
    let active = true;
    let inFlight = false;
    const controller = new AbortController();
    async function refresh() {
      if (inFlight) return;
      inFlight = true;
      try {
        const response = await fetch(url, {cache: 'no-store', signal: controller.signal});
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const value = await response.json();
        if (url.endsWith('/manifest.json') && (!value || value.schema_version !== 1 || typeof value.generated_at !== 'string' || !Array.isArray(value.repositories) || !Array.isArray(value.documents) || !Array.isArray(value.reports))) {
          throw new Error('잘못된 공개 데이터 형식');
        }
        if (url.endsWith('/benchmarks/latest.json') && (!value || value.schema_version !== 1 || typeof value.measured_at !== 'string' || !value.summary || !value.environment || !value.source_revisions)) {
          throw new Error('잘못된 측정 데이터 형식');
        }
        if (active) {setData(value); setError(''); setCheckedAt(new Date().toISOString());}
      } catch (cause) {if (active) setError(cause instanceof Error ? cause.message : String(cause));}
      finally {inFlight = false;}
    }
    void refresh();
    const interval = window.setInterval(() => void refresh(), 60_000);
    return () => {active = false; controller.abort(); window.clearInterval(interval);};
  }, [url]);
  return {data, error, checkedAt};
}
export function timestamp(value?: string | null) {
  if (!value) return '시각 정보 없음';
  const date = new Date(value);
  return Number.isFinite(date.getTime()) ? date.toLocaleString() : '시각 정보 없음';
}
export function LiveStatus({generatedAt, checkedAt, error}: {generatedAt?: string; checkedAt: string | null; error: string}) {
  const stale = generatedAt && Date.now() - Date.parse(generatedAt) > 15 * 60_000;
  return <div className="text-sm text-muted leading-7 mb-8" aria-live="polite">
    {generatedAt && <p>데이터 생성: {timestamp(generatedAt)}{stale ? ' · 오래된 스냅샷' : ''}</p>}
    {checkedAt && <p>최근 조회: {timestamp(checkedAt)} · 브라우저에서 60초마다 확인</p>}
    {error && <p role="alert">갱신 실패: {error}. {checkedAt ? '마지막으로 받은 데이터를 표시합니다.' : '아직 데이터를 받지 못했습니다.'}</p>}
  </div>;
}
export function SourceLinks({documents}: {documents: SourceDocument[]}) {
  return <div className="border-t border-line">{documents.map(document => <article key={document.url} className="border-b border-line py-6">
    <div className="flex flex-wrap justify-between gap-3"><h3 className="text-xl font-semibold"><a className="link" href={document.url}>{document.title} ↗</a></h3>{document.date && <time className="font-mono text-sm text-muted">{document.date}</time>}</div>
    <a className="link inline-block text-sm mt-3" href={viewerLink(document.url)}>사이트에서 읽기 / Read on this site</a><p className="text-sm text-muted mt-3">{document.repository} · <code className="break-all">{document.commit}</code></p>
    <details className="text-xs text-muted mt-3"><summary className="cursor-pointer">파일 검증 정보</summary><p className="break-all mt-2">{document.path}<br/>SHA-256: {document.sha256}</p></details>
  </article>)}</div>;
}
