# Rust 비교 및 Forge DB CPU 최적화 — 2026-10-05

게시물 목록의 DB JSON 집계를 API의 타입 기반 인코딩으로 옮기고, 댓글 수를 기존 인덱스로 계산할 수 있게 수정했다. 실시간 DB·IP 차단 검사는 유지했다. 동시 요청 16에서 최종 게시물 처리량은 기존 5863.23 → 6307.23 req/s (+7.57%), 요청당 DB CPU는 735.35 → 569.12µs (-22.61%)였다. 같은 2,000 req/s 부하에서 요청당 합산 API+DB CPU는 5.93% 감소했다. 처리량을 줄여 DB CPU를 낮춘 결과로 해석하지 않도록 같은 부하 비교와 CPU/요청을 함께 기록했다.

## 변경과 Rust 구현의 차이

- `posts.fg`는 타입별 열과 댓글 수를 조회한다. `native/adapter.c`의 C 헬퍼가 응답 버퍼에 JSON을 생성한다. 크기 경계·할당 실패·int64 범위·문자 이스케이프를 검사하며, 버퍼는 HTTP 라이브러리가 복사한 뒤 해제한다. 숫자 객체·행별 JSON 파싱·반복 포맷팅을 피한다. 기존 타임스탬프 표현을 유지하기 위해 PostgreSQL의 `to_json(timestamptz)`는 유지한다.
- 댓글 집계는 `COUNT(c.id)` 대신 `COUNT(c.post_id)`다. 댓글 PK와 FK는 NULL이 아니며 LEFT JOIN 미일치에서는 둘 다 NULL이므로 개수가 같다. 기존 `(post_id,created_at)` 인덱스로 필요한 열을 읽을 수 있으며 새 인덱스나 마이그레이션은 추가하지 않았다.
- 공개 목록의 차단 검사는 같은 SQL 문 안에 있다. 차단되면 `LIMIT 0`으로 게시물·댓글 집계를 실행하지 않고, 허용되면 `LIMIT NULL`로 전체 목록을 반환한다. 집계 행마다 차단 조건을 평가하지 않도록 변경했으며 새 데이터와 전체 공개 조건의 SQL 회귀도 보완했다. 비공개 글·관리자 우회·빈 배열·CORS·최신순·JSON 정수 타입을 유지했다.
- health는 앞서 검증한 최대 64개 실시간 일괄 검사 구현을 유지한다. 캐시나 DB 검사 제외를 도입하지 않았다. 대기 중인 요청은 10초 제한, 선택된 요청은 DB 정리 뒤 반환, DB 실패는 차단 후 새 요청에서 재검사한다. 초기 health 개선 수치는 [이전 보고서](health-optimization-2026-10-04.md)에 있다.
- Rust의 `BanStore`는 `RwLock<HashMap<...>>`에서 IP 차단을 확인한다. Forge는 PostgreSQL의 현재 상태를 조회한다. 따라서 Rust health의 DB CPU가 거의 0인 것은 검사 정책 차이도 포함하며 언어만의 차이가 아니다. Rust 목록은 DB 열을 `PostSummary`로 변환하고 JSON 응답을 생성한다. 이번 Forge 목록도 같은 방향으로 DB 밖에서 JSON을 생성한다.

이번 비교는 각 애플리케이션과 네이티브 라이브러리를 포함한 구현 비교다. Forge API의 라우팅·검증·SQL 조율은 FG이고 JSON 인코더는 C 헬퍼다. 언어 자체의 속도를 증명하는 실험으로 해석하지 않는다. Rust 소스와 실행 파일은 수정하지 않았다.

## 동일 부하에서의 CPU

게시물 100개, 모두 공개, 댓글 0개, 동시 요청 16, 목표 2,000 req/s:

| 구현 | 실제 req/s | DB CPU µs/요청 | API CPU µs/요청 | 합산 CPU µs/요청 | p95 ms |
|---|---:|---:|---:|---:|---:|
| Rust | 1998.90 | 860.73 | 464.89 | 1331.52 | 3.538 |
| 기존 Forge | 1999.57 | 834.50 | 191.24 | 1025.74 | 2.907 |
| 최종 Forge | 1999.53 | 658.26 | 306.61 | 964.87 | 2.871 |

health, 동시 요청 16, 목표 4,000 req/s:

| 구현 | 실제 req/s | DB CPU µs/요청 | API CPU µs/요청 | 합산 CPU µs/요청 | p95 ms |
|---|---:|---:|---:|---:|---:|
| Rust | 3998.71 | 0.05 | 92.71 | 92.77 | 1.730 |
| 기존 Forge | 3999.53 | 116.55 | 125.94 | 242.49 | 2.329 |
| 최종 Forge | 3998.86 | 115.02 | 122.16 | 238.57 | 2.559 |

요청 시작을 시간표에 맞췄으며 목표·실제 요청 수와 시작 지연도 원본에 기록했다. 표의 p95는 실제 요청 시작부터 응답까지이며 시간표 대기는 제외한다. DB CPU를 API 쪽으로 옮기면 API CPU가 늘 수 있으므로 합산 CPU도 비교했다. CPU 값은 cgroup의 user+system 누적 시간 차이이고, 100%는 CPU 한 코어다. DB 컨테이너 전체의 작은 백그라운드 작업도 포함한다. health 8,000 req/s 후보 측정은 부하 생성기 CPU가 약 200%에 도달하고 시간표 지연이 커 최종 동일 부하 표에서는 제외했다. 원본은 [부하 생성기 한계 측정](db-cpu-optimization-2026-10-05-health-load-limited.json)에 남겼고 최종 health는 4,000 req/s로 다시 측정했다. 이 health 측정에서 최종 DB CPU/요청 변화는 -1.31%, 합산 CPU/요청은 -1.62%였다. health 추가 최적화의 성과로 주장하지 않으며 측정값을 그대로 보관했다.

## 최대 부하 비교

| 경로·동시 요청 | 구현 | req/s | p95 ms | DB CPU µs/요청 | API CPU µs/요청 |
|---|---|---:|---:|---:|---:|
| /api/health · 1 | Rust | 2963.81 | 0.555 | 0.09 | 71.43 |
| /api/health · 1 | 기존 Forge | 1550.89 | 1.092 | 146.65 | 133.88 |
| /api/health · 1 | 최종 Forge | 1585.78 | 1.174 | 155.89 | 125.84 |
| /api/health · 16 | Rust | 11501.73 | 1.843 | 0.01 | 76.50 |
| /api/health · 16 | 기존 Forge | 9617.85 | 2.251 | 62.68 | 103.65 |
| /api/health · 16 | 최종 Forge | 9751.84 | 2.289 | 64.40 | 108.78 |
| /api/posts · 16 | Rust | 3635.45 | 7.027 | 759.64 | 409.55 |
| /api/posts · 16 | 기존 Forge | 5863.23 | 3.881 | 735.35 | 194.78 |
| /api/posts · 16 | 최종 Forge | 6307.23 | 3.648 | 569.12 | 301.07 |
| /api/posts · 64 | Rust | 3559.31 | 33.471 | 771.52 | 408.09 |
| /api/posts · 64 | 기존 Forge | 5889.05 | 19.734 | 725.19 | 189.50 |
| /api/posts · 64 | 최종 Forge | 6390.77 | 18.662 | 561.68 | 299.62 |

각 구성에 API 2 CPU·512 MiB·DB 풀 5개를 적용했다. DB는 CPU 제한 없는 별도 tmpfs 컨테이너다. 단일 동시 요청은 부하 프로세스 1개, 나머지는 2개를 사용했다. Docker healthcheck는 측정 중 꺼서 별도 검사 요청이 섞이지 않게 했다. 각 경우 1초 예열 뒤 4초씩 3회, 구성 순서를 회전하여 측정했다. 모든 최종 HTTP 측정은 오류 0이었다. 공유 호스트이고 단일 요청의 스케줄 지연 및 부하 생성기의 CPU 한계가 있으므로 작은 차이를 확정적 성능 차이로 주장하지 않는다.

단일 health 처리량 중앙값은 기존 1550.89 → 최종 1585.78 req/s (+2.25%), p95는 1.092 → 1.174ms였다. 최종 health 본문과 재배치 주소를 제외한 기계어 명령열은 기존과 같았다. 이 결과에서 모든 부하의 속도 동등성을 주장하지 않는다. 동시 요청 16 처리량은 기존 9617.85 → 최종 9751.84 req/s였다.

최종 health 계측은 요청당 DB 쿼리가 1개 이하이며, 중앙값으로 쿼리 한 번이 1.91개 요청을 처리했다. 이 계측용 실행 파일은 일반 성능 표의 실행 파일과 구분했다.

## 댓글 20만 개의 SQL 검증

각 경우 3회 예열과 12회 측정을 한 세션의 prepared query로 실행했다. 세 쿼리의 순서를 회전했고 결과 행 동일성 및 차단 시 posts/comments 실행 횟수 0을 확인했다. 아래 값은 EXPLAIN ANALYZE의 SQL 실행 시간 중앙값이며 DB CPU나 HTTP 처리량이 아니다.

| 데이터 | 기존 DB JSON ms | 중간 COUNT(post_id) ms | 최종 LIMIT 차단 게이트 ms |
|---|---:|---:|---:|
| http_fixture | 0.663 | 0.396 | 0.464 |
| comment_selective_fresh | 22.282 | 22.056 | 3.327 |
| comment_selective_visible | 21.595 | 1.222 | 0.964 |
| comment_all_visible | 64.470 | 67.660 | 35.715 |

`comment_selective_visible`는 게시물 1,000개 중 공개 20개·댓글 200,000개이며 VACUUM 이후 index-only scan의 heap fetch는 0이었다. 이 조건에서는 기존 JSON 21.595ms에서 최종 0.964ms로 줄었다. 갓 삽입한 선택적 공개 조건에서도 기존 22.282 → 최종 3.327ms였다. 모두 공개인 1,000개 글에서도 기존 64.470 → 최종 35.715ms였다. 초기에 관측한 전체 공개 회귀를 확인한 뒤 차단 게이트를 집계 밖의 LIMIT으로 옮겨 보완했다. 전체 집계 비용과 visibility map 상태에 따라 효과가 달라지며 모든 데이터에서 같은 개선을 보장하지 않는다.

## 정확성과 기동 검사

최종 바이너리로 통합 검사 30/30, 네이티브 CTest 3/3, 프록시 보안·health lifecycle 검사를 통과했다. 통합 검사는 DB 풀 1개·지연된 GitHub·즉시 차단/해제·DB 실패·동시 요청·관리자·초안·큰 int64·Unicode/제어문자·날짜·빈 목록을 포함한다. lifecycle 검사는 대기 80개 제한, 선택된 리더의 연결 종료, DB 세션 종료 후 복구, 대기 요청 중 SIGTERM 종료를 확인했다. migration 11과 재기동도 통과했다.

PostgreSQL 초기화용 임시 서버에서는 Unix `pg_isready`가 성공(0)하지만 API 주소의 TCP 검사 결과는 연결 불가(2)였다. 최종 서버가 뜬 뒤 동일 TCP 검사가 성공했고 다른 컨테이너에서 인증된 `SELECT 1`까지 성공했다. `portfolio_check.py`는 API가 사용하는 DB 호스트·포트·DB 이름으로 TCP를 확인한다. 일회용 자원은 검사 후 제거했다.

health IP 중복 제거와 200µs 수집 대기 후보도 측정했다. 단일 요청이나 p95 응답 지연에서 회귀가 관측되어 최종 구현에서 제외했다. 최종 health 본문은 이전 검증 구현과 바이트 단위로 같으며 SHA-256은 `f92a81527b886cf7ef8c14164903caba8944f9de4f644bcfbd1d45db0ecee128`이다. 새 DB CPU 개선의 핵심은 게시물 경로다.

## 셀프 호스팅 컴파일러

사용자가 지정한 두 범위 중 컴파일러도 별도 완료했다. 내장 함수 식별자 조회를 줄여 같은 현재 입력에서 stage2 C 변환 wall 중앙값 17.037 → 13.316ms, CPU 16.804 → 12.908ms였다. 실행 검사 20/20과 stage2→stage3→stage4 고정점 검증을 통과하고 작업 디렉터리의 stage2를 갱신했다. 개선 전·후 생성 C는 같았다. 현재 C stage0보다 stage2는 여전히 느리며, Rust와 짧은 정수 실행 비교는 변동 범위가 커 언어 우위를 주장하지 않는다. 상세 조건과 원본은 [셀프 호스팅 보고서](selfhosting-performance-2026-10-05.md)에 있다.

## 재현 및 산출물

API 작업 디렉터리: `/home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration`. Forge 보고서·벤치마크는 `/home/helloworld0822/coding/forge`다. 최종 런타임 이미지는 `forge-portfolio-improved:1791188026654457042`, image ID는 `sha256:9c68165c795e72dd7320ca727b9c81683d8428b85414651ac77275566c3ef392`, 실행 파일 SHA-256은 `db432033c1fc58cd75c48a6ccb8f397934a874ad720bf0677b8eb45d8a5d1b75`다. 최종 소스·실행 파일·검사 명령과 각 단계 종료 코드는 [요약·검증 JSON](db-cpu-optimization-2026-10-05-summary.json)에 있다. 소스 스냅샷과 빌드 디렉터리도 그 JSON에 기록했다.

[최대 부하 36회](db-cpu-optimization-2026-10-05-max.json), [health 동일 부하 9회](db-cpu-optimization-2026-10-05-fixed-health.json), [posts 동일 부하 9회](db-cpu-optimization-2026-10-05-fixed-posts.json), [health 계측 3회](db-cpu-optimization-2026-10-05-profile-health.json), [SQL 실행 계획](db-cpu-optimization-2026-10-05-sql.json), [Unix/TCP 초기화 재현](db-cpu-optimization-2026-10-05-readiness.json)을 보관했다. 앞선 [Rust/Forge 비교](rust-forge-comparison-2026-10-04.md)의 수치는 소급 변경하지 않았다.

보존된 준비 파일·이미지·실행 파일이 있을 때 아래 명령을 새 출력 파일로 실행하면 된다. API/DB 자원은 일회용이며 실제 서비스나 `.env`를 읽지 않는다. 운영 배포는 하지 않았다.

```sh
/tmp/forge-benchmark-env/bin/python benchmark/portfolio_diagnose.py \
  --prepared /tmp/forge-continuation-20261003/portfolio-comparison-prepared.json \
  --profile-dir /tmp/forge-db-cpu-limit-20261005 \
  --runtime-image forge-portfolio-improved:1791188026654457042 --improvement \
  --client-processes 2 --repeats 3 --seconds 4 \
  --variants rust plain_baseline_connection plain_fixed_connection \
  --output /dev/shm/forge-db-cpu-reproduction.json
```

동일 부하 비교에는 `--posts-only --concurrency 16 --rate 2000`, 또는 `--health-only --concurrency 16 --rate 4000`을 추가한다.
