# Forge 애플리케이션 성능 개선 및 Rust 비교 — 2026-10-03

최종 실행 파일로 `/api/posts`를 재측정했다. 동일한 계측 바이너리에서 기존 pool/auto 설정을 새 connection/auto 설정으로 바꾸면, 동시 요청 16/64의 RPS 중앙값은 **36.51% / 5.94% 증가**, p95 중앙값은 **52.20% / 39.57% 감소**했다. 이 비교에는 스레드 방식과 polling 방식 변경이 함께 들어 있다. polling을 양쪽 모두 `poll`로 맞춘 추가 대조 실험에서 RPS 중앙값 차이는 **1.30% / 0.48%**, p95 감소는 **30.12% / 7.40%**였다. RPS 범위가 겹치므로 스레드 변경만으로 일관된 처리량 우위를 입증했다고 해석하지 않는다.

최종 비계측 Forge와 기존 Rust release 애플리케이션의 `/api/posts` RPS 중앙값 차이는 **+63.54% / +66.76%**였다. 공개 게시물 100개, 댓글 0개인 이 테스트 데이터와 실행 환경에서 나온 애플리케이션 비교다. 별도 SQL 데이터에서는 복구한 댓글 인덱스로 특정 게시물의 댓글 조회가 **22.8225 → 2.9260 ms**, 약 **7.80배** 빨라졌다. 공개 게시물 목록의 JSON 쿼리는 **34.4995 → 51.6530 ms**로 느려져 목록 속도 개선의 근거가 되지 않았다.

최종 40회 실험의 요청 814,017건과 poll 대조 20회의 464,524건에서 측정 오류와 API cgroup CPU throttling은 모두 0이었다. 이전 60회 진단도 1,332,948건의 오류가 0이었지만 다른 바이너리와 이전 측정 경계를 사용했으므로 최종 결과와 합쳐 평균을 내지 않았다.

## 실제 반영 범위

수정한 포트폴리오 애플리케이션 소스는 `/home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration`에 있다.

- `backend-forge/vendor/forge-web/src/bridge.c`: 기본값을 `FORGE_WEB_THREADS=connection`으로 변경했다. 요청의 동기식 PostgreSQL 작업이 libmicrohttpd의 소수 pool worker를 막는 상황을 완화한다. connection 모드는 `MHD_USE_THREAD_PER_CONNECTION`과 `MHD_USE_ITC`를 함께 사용하고 pool 크기 옵션을 전달하지 않는다. `FORGE_WEB_THREADS=pool`도 유지했다.
- connection 모드에서 epoll 요청을 poll로 조정하고, 필요한 경우 select로 안전하게 재시도한다. 기본 auto의 pool 모드는 이 환경에서 epoll을 사용하고 connection 모드는 poll을 사용하므로 기본 설정 비교에 이 효과가 포함된다.
- `backend/migrations/0011_restore_comment_post_index.sql`: `comments(post_id, created_at)`의 `comments_post_id_created_at_idx`를 `CREATE INDEX IF NOT EXISTS`로 복구한다. 최초 인덱스가 migration 0009의 열 삭제 과정에서 사라진 것을 새 migration으로 보완했다. 기존 migration을 수정하지 않았다. Forge migration 등록과 integration 기대 개수는 11로 갱신했다.
- 부하 생성기는 자식 프로세스 준비 확인, 공통 시작 시각, 실제 시작 지연 기록, 측정 창을 맞춘 cgroup CPU 계산과 실패 시 자식 종료/회수 처리를 갖춘다. profiler는 연결을 처리한 스레드 생애별 요청 분포와 동기 DB 대기 구간을 기록한다.

정책은 유지했다. 각 요청에서 PostgreSQL의 최신 차단 상태를 확인하며, 메모리 차단 캐시로 교체하지 않았다. 동기식 `PQexecPrepared`도 그대로 사용한다. Forge 일반 스케줄러나 새 문자열 API 최적화는 이 애플리케이션의 생성된 native 요청 경로에서 호출되지 않으므로 이번 결과를 그 최적화의 효과 또는 언어 전체의 속도로 설명할 수 없다.

## 실험 조건과 통계

AMD BC-250 공유 호스트, Linux 6.8, API 컨테이너별 CPU 2개·메모리 512 MiB 제한이다. PostgreSQL 컨테이너에는 같은 2 CPU 제한을 걸지 않았다. CPU 100%는 코어 1개 사용에 해당한다. DB, API와 두 개의 Python/aiohttp 부하 프로세스가 호스트 자원을 공유한다.

최종 결과는 구성 4개 × 동시 요청 16/64 × 5회 = 40회다. 추가 poll 대조는 구성 2개 × 16/64 × 5회 = 20회다. 각 측정 전에 1초 warmup 후 4초 동안 요청을 시작하고 진행 중인 요청은 완료될 때까지 처리한다. 마지막 요청의 지연과 실제 경과 시간도 포함한다. 생성기 연결 수는 프로세스 두 개에 8+8 또는 32+32로 나눈다. 반복마다 구성 순서를 순환하며, 동일 repeat 번호를 paired 계산에 사용한다. 서로 다른 구성은 동시에 실행되지 않는다.

RPS와 p95 표의 수치는 **회차별 값의 중앙값 및 최소–최대**다. p95는 각 회차의 요청 지연 p95를 다시 중앙값으로 요약한 것으로, 모든 요청을 합친 p95가 아니다. 클라이언트 percentile은 정렬된 표본에서 구현된 `int(n*p)-1` 위치를 사용한다. 결과의 백분율은 별도 표시가 없으면 **중앙값의 비율**이며, paired 비율의 중앙값은 아래에서 구분했다. 5회와 공유 호스트의 범위만으로 통계적 유의성이나 다른 환경의 성능을 보장하지 않는다.

Rust는 기존 Rust 1.89/Debian glibc release 이미지와 thin LTO 설정을 사용한다. Forge native 애플리케이션은 Alpine musl/GCC 13.2.1의 `-O2`, native 의존성은 Release로 빌드했다. libc, 프레임워크, SQL/JSON 구성과 차단 정책 경로가 같지 않다. 따라서 비교는 실제 배포 애플리케이션의 관측 결과이며 동일 작업의 순수 언어 성능 대조가 아니다.

## 최종 40회: 같은 바이너리 설정 비교와 Rust 비교

| 구성 | 동시 요청 | RPS 중앙값 (범위) | p95 ms 중앙값 (범위) |
|---|---:|---:|---:|
| Rust release | 16 | 3600.00 (3564.73–3616.72) | 7.111 (7.013–7.182) |
| Rust release | 64 | 3628.45 (3605.51–3653.86) | 33.087 (32.562–33.493) |
| Forge 계측 pool / auto | 16 | 4320.62 (2280.16–4819.25) | 8.174 (5.445–13.572) |
| Forge 계측 pool / auto | 64 | 5729.15 (5248.70–5779.91) | 31.875 (23.057–35.034) |
| Forge 계측 connection / auto→poll | 16 | 5898.15 (5764.21–6025.98) | 3.907 (3.774–4.079) |
| Forge 계측 connection / auto→poll | 64 | 6069.49 (5957.95–6104.33) | 19.261 (19.025–19.757) |
| Forge 비계측 connection / auto→poll | 16 | 5887.60 (5843.18–5900.59) | 3.892 (3.864–3.992) |
| Forge 비계측 connection / auto→poll | 64 | 6050.78 (6012.62–6127.84) | 19.152 (19.006–19.348) |

`profile_fixed_pool`과 `profile_fixed_connection`은 동일한 최종 계측 실행 파일 및 runtime 이미지다. 이 표의 전/후는 그 파일의 설정을 바꾼 비교다. 변경 전의 오래된 실행 파일과 최종 파일을 새로 같은 조건에서 직접 대조한 결과로 부르지 않는다. 실제 기본값 변경의 전체 효과를 보여주며, 스레드 방식의 단독 효과는 다음 poll 대조로 제한한다.

회차를 맞춘 RPS 비율(connection/pool)의 중앙값은 동시 요청 16에서 **1.3651** (1.2288–2.5605), 64에서 **1.0655** (1.0501–1.1351)이다. 64의 중앙값 비율 **1.0594**와 paired 중앙값 **1.0655**는 다른 통계다. 비계측 Forge/Rust paired 비율의 중앙값은 16에서 **1.6354** (1.6315–1.6475), 64에서 **1.6626** (1.6549–1.6912)다. 따라서 64의 paired 개선률은 66.26%, 중앙값 비율 개선률은 66.76%다.

## polling을 맞춘 20회 대조

| 구성 | 동시 요청 | RPS 중앙값 (범위) | p95 ms 중앙값 (범위) |
|---|---:|---:|---:|
| Forge 계측 pool / poll | 16 | 5735.80 (5233.97–5752.14) | 5.694 (4.692–7.186) |
| Forge 계측 pool / poll | 64 | 5971.11 (5331.21–6057.18) | 21.015 (19.916–22.904) |
| Forge 계측 connection / poll | 16 | 5810.36 (5595.70–5870.80) | 3.979 (3.886–4.300) |
| Forge 계측 connection / poll | 64 | 6000.03 (5971.74–6060.15) | 19.459 (19.162–19.499) |

이 대조는 같은 계측 바이너리에서 pool/`poll`과 connection/`poll`을 비교한다. connection/auto는 현재 구현상 poll로 선택되므로, 기본 auto 대조보다 스레드 방식에 대한 더 강한 통제다. 연결 관리와 worker 배치도 함께 달라지므로 완전히 추상화된 pthread 단독 실험은 아니다.

중앙값 비율의 RPS 증가는 16에서 **1.30%**, 64에서 **0.48%**, p95 감소는 **30.12% / 7.40%**다. paired RPS 비율 중앙값은 **1.0235 / 1.0030**, 범위는 **0.9753–1.1101 / 0.9884–1.1367**이다. 일부 회차에는 pool이 더 빠르므로 처리량 향상이 반복마다 나타난다고 할 수 없다. 기본 auto 비교의 큰 RPS 상승을 모두 connection 스레드 방식에 귀속하지 않는다.

## 자원과 다음 병목

| 구성 | 동시 요청 | API CPU % | DB CPU % | 부하 생성기 CPU % | 스레드 중앙값 (범위) | 메모리 중앙값 / 관측 최대 MiB |
|---|---:|---:|---:|---:|---:|---:|
| Rust | 16 | 141.93 | 264.88 | 132.05 | 4 (4–8) | 8.8750 / 9.0391 |
| Rust | 64 | 141.73 | 264.49 | 132.92 | 4 (4–7) | 8.8125 / 9.1211 |
| 계측 pool | 16 | 71.28 | 298.87 | 149.99 | 9 (9–9) | 5.0859 / 5.3242 |
| 계측 pool | 64 | 99.21 | 399.74 | 180.09 | 9 (9–9) | 11.6094 / 11.6367 |
| 계측 connection | 16 | 111.19 | 414.66 | 184.42 | 18 (18–18) | 5.8086 / 6.0312 |
| 계측 connection | 64 | 109.26 | 414.87 | 186.82 | 66 (66–66) | 14.6523 / 15.3516 |
| 비계측 connection | 16 | 110.62 | 415.41 | 183.66 | 18 (18–18) | 5.6758 / 5.8555 |
| 비계측 connection | 64 | 108.74 | 415.28 | 187.23 | 66 (66–66) | 13.8242 / 14.0039 |

메모리는 cgroup `memory.current` 관측값이다. 약 200 ms 주기로 읽은 최대이며 짧은 peak를 놓칠 수 있다. 동시 요청 64의 최종 계측 connection 최대는 **15.3516 MiB**, 비계측 최대는 **14.0039 MiB**다. 이전 자료의 ‘15 MiB 미만’을 최종 계측 결과에 적용하면 안 된다.

Forge 비계측 생성기 CPU 중앙값은 183.66% / 187.23%이고 개별 자식 CPU 최대는 92.97% / 94.96%였다. 클라이언트 프로세스가 두 코어에 가까워지고 DB도 4코어 이상을 사용하는 구간이다. API CPU가 제한치보다 낮다는 이유만으로 API에 남은 처리량 여유를 확정하거나 해당 RPS를 최대 처리량으로 부를 수 없다.

클라이언트 CPU는 실제 요청 경과 시간을 분모로 쓴다. API·DB cgroup CPU는 공통 시작 전 100 ms 준비 구간과 결과 수집/자식 종료까지 포함한 관측 경과 시간을 분모로 사용한다. 최종 40회에서 요청 창은 **4.002–4.024초**, cgroup 관측 창은 **4.1587–4.1957초**, 최대 시작 지연은 **1.0428 ms**였다. poll 대조의 관측 창은 **4.1615–4.1869초**, 최대 시작 지연은 **0.6801 ms**였다. 서로 다른 분모의 CPU 수치로 직접 CPU/요청 인과 효과를 계산하지 않는다.

다음 표는 각 회차의 요청당 평균 계측값을 다시 중앙값으로 요약했다. 핸들러 내부만 계측하므로 MHD 진입 전 queue와 응답 전송은 포함하지 않는다. 서로 다른 구간의 중앙값을 더하면 핸들러 중앙값과 일치할 필요가 없다.

| 계측 구성 | 동시 요청 | 핸들러 경과 µs | 핸들러 CPU µs | PG 연결 대기 µs | SQL µs | 대기 / 핸들러 % |
|---|---:|---:|---:|---:|---:|---:|
| pool / auto | 16 | 781.19 | 78.82 | 0.45 | 759.78 | 0.06 |
| pool / auto | 64 | 911.78 | 90.95 | 105.94 | 779.38 | 11.62 |
| connection / poll | 16 | 1589.24 | 100.14 | 781.05 | 794.12 | 49.15 |
| connection / poll | 64 | 8824.36 | 94.95 | 8021.88 | 775.57 | 90.94 |

connection 모드의 동시 요청 64에서 핸들러 시간의 약 **90.94%**가 PostgreSQL pool 연결 획득 대기다. 현재 pool 크기는 5다. HTTP worker가 동기 쿼리로 막히는 문제를 완화한 뒤 DB lease 대기가 뚜렷해진 것이다. DB와 생성기 CPU도 높아졌으므로 pool을 무조건 늘리기보다 DB 쿼리와 pool 크기를 별도로 통제해 재측정해야 한다.

최종 40회와 poll 20회의 모든 계측 회차에서 `requests == queries == client requests == sum(worker_requests)`를 확인했다. histogram bin은 재사용 가능한 worker 번호가 아니라 **스레드 생애마다 발급한 ID**다. 예를 들어 connection 모드에서 활성 bin이 16/64개여도 프로세스의 전체 생애에 정확히 그 수의 스레드만 생성됐다는 뜻은 아니다. profiler는 마지막 bin 사용 시 결과를 거부하고, 최종 40회의 최고 사용 ID 164는 한도 512 이하였다.

HTTP 연결 cap은 256, 연결 timeout은 30초, MHD 연결 메모리 한도는 128 KiB다. cap 256은 전체 프로세스 스레드 수 제한이 아니다. idle keepalive도 connection 스레드를 점유할 수 있고 pthread stack 및 스레드별 allocator arena는 가상 주소 공간을 추가로 사용한다. Forge TLS 문자열 arena는 문자열을 사용한 스레드마다 최초 4 MiB 용량을 할당하고 요청 종료의 reset 후에도 스레드 종료까지 블록을 보유한다. 이는 용량·주소 공간 수치이며 같은 크기의 resident RSS를 뜻하지 않는다. metadata의 `fetch_many`는 handler마다 helper 스레드를 최대 4개 추가할 수 있어 연결 cap만으로 전체 스레드 수를 제한하지 못한다. 64개의 짧은 실험에서 작은 memory.current를 관측한 것으로 256 연결의 장시간 RSS·가상 메모리 상한이나 512 MiB에서의 최악 조건을 검증했다고 주장하지 않는다.

## 댓글 인덱스: 별도 SQL 실험

HTTP 처리량 데이터는 공개 게시물 100개와 댓글 0개다. SQL 실험 데이터는 **게시물 1,000개(공개 20개), 댓글 200,000개**다. 두 데이터셋의 결과를 합쳐서 인덱스가 위의 HTTP RPS 상승을 만들었다고 설명하지 않는다. HTTP 비교의 Rust/Forge DB 모두 같은 인덱스를 복구한 상태로 측정했다.

각 쿼리를 인덱스 없음/복구 후에 7회 `EXPLAIN (ANALYZE, BUFFERS, TIMING OFF, FORMAT JSON)`으로 실행했다. 각 단계의 첫 실행을 동일하게 제외하고 남은 6회의 Execution Time 중앙값을 계산했다. 순차 단계와 공유 호스트 결과이므로 cache/실행 순서 효과도 남아 있다.

| SQL 경로 | 인덱스 없음 ms | 복구 후 ms |
|---|---:|---:|
| 게시물 목록 rows | 41.4355 | 41.7955 |
| 게시물 목록 JSON | 34.4995 | 51.6530 |
| 차단 검사 포함 목록 JSON | 45.7180 | 42.3465 |
| 특정 게시물 댓글 | 22.8225 | 2.9260 |
| 차단 조회만 | 0.0765 | 0.0660 |

특정 게시물 댓글 조회만 기존 parallel sequential scan에서 복구한 `comments_post_id_created_at_idx`의 bitmap index scan 경로로 바뀌었다. 중앙값 기준 **7.80배**, **87.18%** 시간 감소다. 목록 쿼리는 댓글 전체를 읽는 경로가 남고 JSON 측정은 오히려 느려졌다. 선택적 댓글 조회에서 확인한 이득을 게시물 목록 JSON이나 전체 API의 이득으로 확대하지 않는다.

fresh DB 및 재시작에서 migration 11과 index `indisvalid/indisready`를 확인했다. 기존 DB 업그레이드는 migration 0009 이후 인덱스가 없는 상태로 fixture를 넣고, 최종 Forge 시작으로 11을 적용했다. 댓글 `count(*)`와 `sum(post_id)`의 보존을 검증했으며 모든 열의 byte 단위 동일성을 검증한 것은 아니다. `IF NOT EXISTS` 직접 재실행도 통과했다. 같은 이름의 다른 정의를 자동 교정하는 migration은 아니므로, 이미 다른 정의가 있는 외부 DB는 별도 확인이 필요하다.

Rust의 기존 migration 파일 loader는 새 파일을 탐색하므로 코드 변경이 필요 없지만, 이번 검증에서 새 Rust 이미지로 migration 11 startup을 다시 실행한 것은 아니다. 일반 `CREATE INDEX`는 쓰기 잠금을 유발할 수 있고 transaction 기반 migration에 `CONCURRENTLY`를 그대로 바꾸어 넣을 수 없다. 운영 DB 적용 시간·잠금은 이번 일회용 DB 실험 범위 밖이다.

## 이전 60회 진단: health 및 생성기 한계

다음은 최종 확정 전 후보의 역사적 진단이다. 구성 5개 × endpoint/concurrency 4개 × 3회, 합계 60회다. 최종 바이너리 SHA와 다르고 최종 runtime image ID, 생성기 SHA, 시작 지연 및 cgroup 관측 창 필드가 없다. 최종 health 재검증 결과로 표시하지 않는다.

| 이전 계측의 구성 | 동시 요청 1: RPS 중앙값 (범위) | 동시 요청 16: RPS 중앙값 (범위) | 동시 요청 16: p95 ms 중앙값 | 동시 요청 16: 생성기 CPU % 중앙값 |
|---|---:|---:|---:|---:|
| Rust | 2629.90 (2135.07–2692.65) | 11307.22 (11009.35–11668.83) | 2.100 | 196.82 |
| 기존 bridge / pool | 1293.34 (1222.37–1769.39) | 9928.77 (9485.35–10031.97) | 2.363 | 196.67 |
| 이전 후보 / pool | 1697.50 (1502.35–1793.61) | 9564.62 (9391.88–9879.11) | 2.445 | 196.77 |
| 이전 후보 / connection 계측 | 1310.30 (1235.30–1895.17) | 9843.13 (9822.95–9991.85) | 2.165 | 196.95 |
| 이전 후보 / connection 비계측 | 1362.66 (980.03–1385.08) | 9864.16 (9347.35–10471.39) | 2.170 | 196.91 |

동시 요청 16의 health는 생성기 CPU가 약 197%여서 두 프로세스의 처리 한계에 가깝다. 동시 요청 1에서는 실제 생성기 하나만 사용하며 결과 편차도 크다. Rust health는 즉시 JSON을 반환하는 경로인 반면 Forge는 요청마다 DB의 차단 정책을 확인한다. 이 결과는 모든 endpoint가 개선됐다는 근거도, 언어 자체가 더 빠르거나 느리다는 근거도 아니다.

이전 단일 프로세스 health 계측에는 회차별 client 요청보다 profiler 요청/쿼리가 정확히 한 건 더 포함되는 경계가 있었다. 최신 40/20회에는 이 불일치가 없고 새 관측 창을 기록한다. 따라서 이전 자료의 CPU 또는 health 계측 값을 최신 값과 섞어 효과를 산출하지 않았다. 생성기 프로세스 수 확대, 장시간 측정과 DB 분리 없이는 최대 용량을 결정하기 어렵다.

## 검증과 팀 검토

[검증 요약 JSON](performance-improvements-validation.json)에 원본 일반 검증 로그의 SHA-256과 범위를 남겼다. native CTest **3/3**, blocking/isolation **5개 설정**, connection keepalive·HTTP **2개 polling 설정**, API integration **26개 테스트**, nginx **2개 설정에 걸쳐 총 12개 보안 assertion**이 통과했다. blocking 검증은 slow handler가 실제 시작된 뒤 다른 연결의 fast 요청을 보내며, 후속 fast 요청과 서버 종료 코드도 확인한다. pool/auto의 fast 지연은 약 1.000초, connection 4개 설정은 약 0.001초였다.

`FORGE_WEB_THREADS`를 지정하지 않은 API 실행에서도 startup 로그의 기본 connection 모드를 확인했다. native fallback의 미지원 feature 조합은 소스 검토 범위다. API 검증에는 pool 크기 1, mock GitHub, commit된 차단의 즉시 반영·해제·만료, 오류 시 비관리자 접근 차단, fresh/업그레이드/restart migration 검증이 포함된다. integration에서 DB가 필요한 테스트도 실행됐고 skip은 없었다. 본 성능 리뷰의 신규 실험이 이전에 작성한 인증·프록시 변경까지 새로 구현했다는 의미는 아니다.

검증 runtime 이미지는 기존 after-release의 고정 image ID 위에 최종 실행 파일과 migration 11을 복사하여 만들었다. 최종 소스 전체의 새로운 multi-stage Docker 빌드 검증으로 부르지 않는다. coordinator가 실험이 소유한 diagnosis/improvement 컨테이너 및 네트워크 제거를 확인했다. 이미지와 증거 파일은 재현용으로 유지했으며 운영 배포나 commit/push는 수행하지 않았다.

OMO 팀 검토는 **실제 Codex worker 3명**이 번호 **1–10의 검토 작업**을 나누어 수행했다. transport는 **MultiAgentV2**다. ‘최대 10개 허용’은 10개의 동시 실행 또는 10명의 서로 다른 worker를 실행했다는 뜻이 아니다. 초기 GLM 출력은 Codex 검증으로 집계하지 않았다. member B는 작업 4(40회 수치), 5(poll 대조), 6(생성기/health), A는 HTTP·수명·경계, C는 schema·정책·패키징을 검토했다.

## 실행 파일·소스 식별과 원본

| 식별 항목 | SHA-256 또는 image ID |
|---|---|
| 최종 runtime image `forge-portfolio-improved:1791036876723055070` | `f0a497df4f13e616260764e3cfd860fb421e66b98321589fa694365927f02a5a` |
| 최종 비계측 `portfolio-fixed` | `0f6a4690708d53db5b43b79ad430c7e1a18f98c17920045f5a7d2962b29ccfc8` |
| 최종 계측 `portfolio-fixed-profile` | `1438c1943d509e0374aefba30c69a18a4c9a95249ccc86d214d374023e2ae25c` |
| 실제 링크한 candidate `bridge.c` | `ff7908bd428bfb19aed12dff9cce15523447ec26e83d011817448164207fb3dc` |
| 최종 생성된 `server.c` | `873a0abbb45b484760ade21325248e7ffaadac17b4446a66e859aa2d12201893` |
| migration 11 | `6a95b105b7c5d7e63decbc6c6d39f037e20b3a98e9f8a2cb9bfc18fc13b93bb1` |
| Rust 비교 이미지 | `06263bc0a2216ddbdc985d1a728983c4d4b95f494cadb53b0f4f3b957918d0da` |
| 준비 manifest의 Rust source digest | `070e558d24e94fa526186b975670ad3b5343a55109478ee3e5a9a98d953de131` |
| 준비 after-release/toolchain 이미지 | `9338d9ed9194f4d57c7d9513e3bb292a0875e69282eed877f81fabc74e60005f` |
| 준비 manifest의 after toolchain source digest | `93acb869e6e9badd8075323fcc24e54234e9acaf00f230ca55adcc821b6cd994` |
| profiler | `eda61ace8c07690b36a22670fe83f722e8eeeef2c49b5cb88b1ad47773ab7e4d` |
| 최종 multiprocess load generator | `56bd71d2fb59f6d0531e813cf07cedc8cf9ce53226568b9e283a91affb1e894d` |

`/tmp/forge-improvement-20261003/final-source`에 보존된 bridge SHA는 `d601e58130a3d5e17e741239322cbc79935db4cacc9f4d4c21fd5d66ad82e614`로 더 오래된 파일이다. 빌드 스크립트가 최종 live candidate web 디렉터리를 별도로 덮어 사용했다. 그러므로 **보존 snapshot + 최종 candidate web overlay**가 소스 조합이다. 보존 snapshot만 최종 web 소스라고 부르면 안 된다. 실제 live bridge와 `verified-binaries/candidate-web.c`는 위의 `ff7908…`로 일치하고 Forge `.fg` 소스·adapter·migration 11은 보존본과 일치한다.

아래 JSON은 측정 원본을 byte 단위로 복사했다. [통계 재계산 JSON](performance-improvements-statistics.json)은 그룹 중앙값, 범위, paired 비율, SQL 첫 실행 제외 결과를 담는다. 오래된 원본의 자유 서술 scope보다 이 보고서의 바이너리/측정 경계 구분을 따른다.

| 원본 복사 | SHA-256 |
|---|---|
| [performance-improvements-verified-posts.json](performance-improvements-verified-posts.json) | `8673c524b1d161122bab3cee2e5df84c9ea067780d5ceefd85675d89cea9883b` |
| [performance-improvements-poll-control.json](performance-improvements-poll-control.json) | `f6d22171ac3ef887ab6cd915863ee08b4697f67ff3a38dbfaca595bd12c5d31d` |
| [performance-improvements-diagnosis.json](performance-improvements-diagnosis.json) | `80c3f4e2c65c7b2ea3b0a4cfb4192ca07636a5466ef5377d485ee64669fff6a1` |
| [performance-improvements-verified-checks-and-sql.json](performance-improvements-verified-checks-and-sql.json) | `5835cbea494761c91b092ea3084ac1a75746430fad7ec54313d632bb2de6f905` |

## 재현 명령

아래 경로는 이번 세션에서 유지한 로컬 증거 경로다. 새 결과 파일과 출력 디렉터리를 사용하며, 명령은 일회용 Docker 테스트를 수행한다. Docker, Python venv/aiohttp와 보존 manifest/이미지/소스가 필요하다. 보고서 작성 단계에서는 이 명령을 다시 실행하지 않았다.

```sh
# 보존 애플리케이션 소스와 실제 최종 web overlay로 다시 빌드
sh benchmark/portfolio_improve_build.sh \
  /tmp/forge-improvement-20261003/final-source \
  forge-continuation-after-release:20261003-053000 \
  /home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration/backend-forge/vendor/forge-web \
  /tmp/forge-reproduce-binaries

# 새 이미지의 API/SQL 검증 (JSON에 새 image 이름/ID 기록)
/tmp/forge-benchmark-env/bin/python benchmark/portfolio_check.py \
  --app /home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration \
  --binary /tmp/forge-reproduce-binaries/portfolio-fixed \
  --prepared /tmp/forge-continuation-20261003/portfolio-comparison-prepared.json \
  --output /tmp/forge-reproduce-checks-and-sql.json

# 이번 최종 결과의 바이너리/이미지를 그대로 사용하는 posts 대조
/tmp/forge-benchmark-env/bin/python benchmark/portfolio_diagnose.py \
  --prepared /tmp/forge-continuation-20261003/portfolio-comparison-prepared.json \
  --profile-dir /tmp/forge-improvement-20261003/verified-binaries \
  --runtime-image forge-portfolio-improved:1791036876723055070 \
  --output /tmp/forge-reproduce-posts.json \
  --improvement --posts-only --client-processes 2 --repeats 5 --seconds 4 \
  --variants rust profile_fixed_pool profile_fixed_connection plain_fixed_connection

# 같은 poll backend를 유지하는 추가 대조
/tmp/forge-benchmark-env/bin/python benchmark/portfolio_diagnose.py \
  --prepared /tmp/forge-continuation-20261003/portfolio-comparison-prepared.json \
  --profile-dir /tmp/forge-improvement-20261003/verified-binaries \
  --runtime-image forge-portfolio-improved:1791036876723055070 \
  --output /tmp/forge-reproduce-poll.json \
  --improvement --posts-only --client-processes 2 --repeats 5 --seconds 4 \
  --variants profile_fixed_connection profile_fixed_pool_poll
```

실험 도구는 image ID와 실험 전용 컨테이너 label을 확인하고 이름이 충돌하면 중단하며, 자신이 만든 컨테이너와 네트워크를 정리한다. 지운 일회용 진단 환경은 wrapper가 보존 manifest에서 다시 준비한다. 새로 빌드한 바이너리의 수치를 비교하려면 새 API 검증 JSON의 image를 `--runtime-image`에, 새 출력 디렉터리를 `--profile-dir`에 지정하고 새 SHA/ID도 결과와 함께 보관한다.

OMO Codex 검토 10건은 모두 기록된 범위 내에서 통과했다. [팀 실행·검토 기록](performance-improvements-agents.json)에 실제 worker 식별, 담당 작업, 검토 원문과 팀 상태 정리 기록을 보관했다. 종료된 Codex worker를 런타임에서 별도로 archive하는 API는 제공되지 않으며, 임시 팀 상태는 기록을 보존한 뒤 삭제했다.
