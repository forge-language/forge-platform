# Rust보다 느린 구간의 원인 분석

측정일: 2026-10-03T13:14:55Z. 대상은 기존 portfolio-platform Rust 백엔드와 동일 데이터의 Forge 백엔드다.

가장 확실한 추가 비용은 Forge의 공개 요청마다 실행되는 DB 기반 IP 차단 검사다. Rust는 메모리의 차단 목록을 검사한다. 특히 `/api/health`는 Rust에서 DB 없이 반환되지만 Forge에서는 DB 연결을 빌린 뒤 SQL을 실행한다. 이번 측정에서 Forge health 핸들러 시간의 약 94%, posts 핸들러 시간의 약 97%가 동기 SQL 호출 안에서 소요됐다.

이 수치는 언어 자체의 실행 속도 차이를 뜻하지 않는다. SQL 호출 시간에는 DB 처리, 통신, libpq 내부 처리와 실행 대기가 모두 포함된다. Forge가 모든 API에서 Rust보다 느리다는 결론도 성립하지 않는다.

## 측정 방법

- 6개 설정 × 3개 요청 조건 × 3회 반복 = 54회. 회당 1초 예열 후 4초 부하, 설정 순서를 회차마다 회전했다.
- HTTP/1.1 keep-alive, `Accept-Encoding: identity`. 단일 Python aiohttp 프로세스에서 부하를 생성했다.
- 앱별 CPU 한도 2코어, 메모리 512 MiB. 격리된 PostgreSQL 16 tmpfs 인스턴스의 별도 DB에 동일한 posts 100개 및 projects 10개를 넣었다. DB에는 앱의 2코어 한도를 적용하지 않았다.
- Rust/Forge 및 계측 실행 파일의 health·posts·projects 응답 내용을 검증했다. 동일 시각의 항목은 SQL 정렬의 동률이므로 ID 정렬과 날짜 표현 정규화 후 비교했다.
- 원본 Forge 이미지, 동일 소스로 재빌드한 비계측 바이너리, 계측 바이너리를 각각 포함했다. 계측 바이너리만 worker 수를 변경할 수 있다.
- 실제 요청 핸들러·연결 획득·SQL 호출을 링크 시 `--wrap`으로 계측했다. 앱의 SQL, 차단 검사, 응답 내용은 그대로다.
- CPU 사용량은 cgroup, 부하 생성기 CPU는 getrusage, 실제 스레드 수는 `/proc/<pid>/task`로 확인했다. 100%는 한 코어다.
- 총 871,262개 부하 요청, 오류 0개. 모든 앱 측정 구간의 CPU quota throttling 횟수는 0이다.
- 공유 호스트: Linux-6.8.0-139-generic-x86_64-with-glibc2.39, AMD BC-250. 기존 서비스가 실행 중인 환경이다.

## 원본 이미지 비교

값은 3회 실행의 중앙값이다. Forge는 이전 보고서의 최적화 후 이미지를 그대로 사용했다.

| 요청 | 동시 요청 | Rust RPS | Forge RPS | Forge/Rust | Rust p95 ms | Forge p95 ms |
|---|---:|---:|---:|---:|---:|---:|
| /api/health | 1 | 2,724.95 | 1,240.11 | 0.455 | 0.619 | 1.368 |
| /api/health | 16 | 7,651.44 | 6,610.30 | 0.864 | 3.357 | 2.836 |
| /api/posts | 16 | 3,640.33 | 3,830.38 | 1.052 | 6.919 | 7.854 |

health 단일 동시 요청에서 Forge 처리량은 Rust보다 54.5% 낮았다. posts 동시 요청 16개에서는 이번 중앙값이 Rust보다 5.2% 높았다. 하지만 Forge posts RPS 범위는 2,396.70~4,845.98이고 Rust는 3,579.56~3,730.39로, Forge의 반복 간 편차가 훨씬 크다. 기존 후속 측정에서 나타났던 posts 성능 저하를 해소했다고 주장할 근거는 없다.

## 핸들러 안에서 소요되는 시간

기본 계측 설정은 HTTP worker 8개, DB 연결 5개다. 아래 값은 각 실행의 요청당 평균 시간의 중앙값이다. SQL 비율과 풀 비율도 실행별 비율의 중앙값으로 계산했다.

| 요청/동시 요청 | 핸들러 μs | SQL 호출 μs | 연결 획득 μs | 스레드 CPU μs | SQL/핸들러 | 연결 획득/핸들러 |
|---|---:|---:|---:|---:|---:|---:|
| /api/health / 1 | 259.558 | 242.857 | 0.462 | 72.995 | 94.14% | 0.182% |
| /api/health / 16 | 147.049 | 137.487 | 0.287 | 39.857 | 93.70% | 0.197% |
| /api/posts / 16 | 815.538 | 794.366 | 0.453 | 79.476 | 97.43% | 0.052% |

계측한 공개 요청에서는 요청당 SQL 호출이 정확히 1회였다. 이 SQL은 차단 검사와 본문 생성을 한 문장으로 합친 쿼리다. 계측 구간은 MHD가 핸들러를 호출한 순간부터 핸들러 반환까지이며, HTTP 이벤트 대기와 응답 전송 및 클라이언트 처리는 포함하지 않는다. 따라서 94~97%를 전체 HTTP 지연의 비율로 읽으면 안 된다. 이미지의 백그라운드 health probe가 겹친 health 구간은 부하 요청보다 계측 요청이 1개 많을 수 있다.

## 확인된 원인과 해석

1. **health에 추가된 DB 왕복과 차단 조회: 확인됨.** Forge의 `dispatch`는 먼저 DB 연결을 획득하고, 공개 health는 `guarded_response`로 전달한다. Rust health는 즉시 JSON을 반환하고 `BanGuard`는 메모리 `HashMap`을 검사한다. health 단일 동시 요청에서 DB CPU 중앙값은 Rust 0.104%, Forge 27.959%였다. SQL 호출 시간만 평균 약 243 μs가 추가되는 구조이며, 전체 속도 격차의 정확한 기여율을 모두 분리한 것은 아니다.
2. **동기 DB 호출이 HTTP worker를 점유하는 구조: 코드와 시간 계측으로 확인됨.** Forge의 `PQexecPrepared`와 연결 획득은 요청 스레드에서 동기 실행된다. Rust의 DB 연결 획득·쿼리는 `await`를 사용한다. Forge posts 핸들러는 평균 약 816 μs 동안 점유되고 SQL 호출이 약 794 μs를 차지한다. DB 호출 중 worker가 다른 연결을 처리하지 못하는 구조는 개선 대상이다. 특정 RPS 차이나 tail latency 전부를 이 구조 때문이라고 단정할 수는 없다.
3. **posts의 SQL·JSON 생성 위치 차이: 확인됨, 성능 기여율은 미분리.** Forge는 차단 검사와 `json_agg`를 DB에서 수행하고 JSON 문자열을 전달한다. Rust는 행을 가져와 모델 변환과 JSON 직렬화를 앱에서 수행한다. 따라서 앱 CPU만 비교하면 처리 비용이 DB로 이동한 부분을 놓친다. 이번 posts 측정의 앱/DB CPU 중앙값은 Rust 150.34%/278.05%, Forge 61.29%/287.39%였다.
4. **부하 생성기 제한과 반복 변동: 측정됨.** health 동시 요청 16개에서 부하 생성기 CPU는 Rust 97.35%, Forge 97.43%로 한 코어에 근접했다. 이 구간의 RPS를 서버 최대 처리량으로 해석할 수 없다. Forge posts의 큰 편차는 재빌드·계측 대조군에서도 나타났다. 공유 호스트 변화, 연결의 worker 배정 및 HTTP 대기 영향을 각각 분리하지 못했으므로, 이를 컴파일러 퇴행 또는 계측 오버헤드로 확정하지 않았다.

## 원인으로 확정할 수 없는 항목

**worker 8개 / DB 풀 5개의 불일치는 이번 부하의 주 병목이라는 근거가 없다.** 기본 설정의 연결 획득 시간은 요청당 0.29~0.46 μs이고 핸들러 시간의 0.05~0.20%에 불과했다. 풀을 8개로 늘린 설정도 posts 처리량이 일관되게 높아지지 않았다.

| 계측 설정 worker/DB 풀 | health 동시 요청 16 RPS | posts 동시 요청 16 RPS | posts RPS 범위 |
|---|---:|---:|---|
| 8/5 | 6,266.06 | 3,812.09 | 3,145.36~4,336.78 |
| 5/5 | 6,239.93 | 4,161.62 | 2,871.86~4,203.99 |
| 8/8 | 6,101.77 | 3,829.17 | 3,222.38~4,115.38 |

**스레드가 많아서 Rust가 빠르다는 설명도 맞지 않는다.** 실측 Rust 전체 스레드는 4개(Actix runtime 이름 2개 포함), Forge 기본 전체 스레드는 9개였다. 5-worker 계측 설정은 전체 6개였다. 스레드 수만으로 효율을 판단할 수 없다.

**이전 scheduler·문자열 최적화는 이 HTTP 경로의 핵심 비용을 바꾸지 않았다.** 최적화 전·후·최종 Forge 컴파일러의 앱 생성 C는 모두 SHA-256 `3d76c53441a44f1eb1814bf09221787268187433c3499f400c23aad63ddaba76`로 동일하다. 이 앱은 native `fw_run` 경로를 사용하며 생성 C에는 coroutine spawn/scheduler 호출이나 새 view/builder API 호출이 없다. 런타임의 공통 초기화 비용까지 동일하다고 주장하는 것은 아니다.

**컴파일·libc 차이는 존재하지만 기여율은 측정하지 않았다.** Forge 앱은 GCC `-O2`, 런타임/stdlib는 CMake Release, Alpine/musl이다. 계측 재빌드 GCC는 13.2.1이다. Rust는 `cargo build --release`, 명시적 `lto = "thin"`, Debian/glibc다. 동일 libc·툴체인 옵션의 대조군 없이 이 차이를 주원인으로 지목하지 않았다.

원본/재빌드/계측 기본 설정의 posts 중앙값은 각각 3,830.38 / 4,890.89 / 3,812.09 RPS였다. 계측 RPS의 정확한 오버헤드를 산정할 만큼 안정적인 대조군이 아니다. SQL 호출 시간과 연결 획득 시간이 차지하는 비율은 모든 계측 설정에서 일관되지만, 설정 변경의 RPS 개선 효과는 확정하지 않는다.

## 수정 우선순위

1. 추가 검토에서 Forge의 현행 계약은 직접 DB 변경의 즉시 반영과 차단 테이블 조회 실패 시 500 응답까지 포함함을 확인했다. 메모리 차단 목록으로 대체하면 이 계약이 달라질 수 있으므로, 현재는 요청마다 권위 있는 DB 검사를 유지하고 HTTP 연결 간 간섭과 쿼리·인덱스 비용부터 개선한다. 캐시 도입에는 이 계약을 별도로 설계해야 한다.
2. `forge-postgres` 비동기 쿼리와 HTTP 이벤트 처리를 연결해 DB 대기 중 worker 점유를 줄인다. 연결 풀 증설만으로는 이번 측정의 병목이 해결되지 않는다.
3. posts의 SQL 계획·JSON 비용과 HTTP worker별 처리량/대기 시간을 별도로 계측한다. 원본 이미지의 큰 편차를 재현한 뒤 변경 효과를 평가한다.
4. 여러 부하 생성기 프로세스 또는 별도 호스트로 서버 한계를 측정하고, 이후 동일 libc·최적화 옵션의 대조군으로 컴파일러와 런타임 자체의 영향을 확인한다.

이번 작업은 진단과 재현 도구를 추가했다. 차단 검사 우회나 운영 앱 변경은 적용하지 않았다.

## 소스 근거

- [Forge 요청 분기](/home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration/backend-forge/src/routes.fg:103), [차단 검사 SQL](/home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration/backend-forge/src/database.fg:19).
- [Forge 동기 libpq 호출](/home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration/backend-forge/vendor/forge-postgres/src/bridge.c:285), [풀 대기](/home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration/backend-forge/vendor/forge-postgres/src/bridge.c:90), [HTTP worker 설정](/home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration/backend-forge/src/server.fg:31).
- [Rust health](/home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration/backend/src/routes/health.rs:10), [메모리 차단 검사](/home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration/backend/src/bans.rs:95), [posts async 쿼리](/home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration/backend/src/routes/posts.rs:15).

## 재현 및 원시 자료

- [54회 원시 결과](rust-performance-diagnosis.json): 이미지 ID, 입력 소스 해시, 계측 바이너리 해시, 전체 스레드 이름, 요청 시간·CPU·SQL 계측치를 포함한다.
- [계측 코드](../benchmark/portfolio_profile.c), [진단 실행기](../benchmark/portfolio_diagnose.py), [격리 빌드 도구](../benchmark/portfolio_profile_build.sh).
- 기존 비교 보고서: [continuation-2026-10-03.md](continuation-2026-10-03.md), [posts 후속 원시 결과](continuation-portfolio-posts16-followup.json).

현재 호스트의 준비된 immutable 이미지/소스 스냅샷으로 재현하는 명령이다. 다른 호스트에서는 `portfolio_compare.py`로 대응하는 스냅샷·이미지 manifest를 먼저 준비해야 한다. 출력 빌드 디렉터리는 비어 있어야 한다.

```sh
sh benchmark/portfolio_profile_build.sh \
  /tmp/forge-portfolio-snapshot-_8ronqk6/after \
  forge-continuation-after-release:20261003-053000 \
  /tmp/forge-rust-diagnosis-rebuild
/tmp/forge-benchmark-env/bin/python benchmark/portfolio_diagnose.py \
  --prepared /tmp/forge-continuation-20261003/portfolio-comparison-prepared.json \
  --profile-dir /tmp/forge-rust-diagnosis-rebuild \
  --output /tmp/forge-rust-diagnosis-rebuild/diagnosis.json \
  --repeats 3 --seconds 4
```

실행기는 기존 동명 컨테이너/네트워크와 변경된 이미지 ID를 거부한다. 이번 진단의 임시 앱·DB·네트워크는 종료 후 모두 제거됐다. 계측 빌드 산출물과 패키지 목록은 `/tmp/forge-rust-diagnosis-20261003`에 두었으며 portfolio-platform에 추가하지 않았다.
