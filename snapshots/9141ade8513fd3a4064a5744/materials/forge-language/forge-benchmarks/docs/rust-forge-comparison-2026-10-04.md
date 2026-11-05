# Rust / 최적화 Forge API 비교 보고서 — 2026-10-04

측정 완료 시각은 `2026-10-04T13:37:18Z`다. 보존된 Rust release와 최적화된 비계측 Forge 실행 파일을 일회용 DB·API 컨테이너에서 새로 비교했다. 구성 2개 × 요청 조건 4개 × 5회, 총 40회에 걸쳐 885,179건을 처리했고 요청 오류는 0건이었다. 아래 수치는 이번 호스트와 데이터에서 관측한 애플리케이션 구현의 차이다.

처리량과 응답 지연은 회차별 수치의 중앙값이다. 처리량 변화는 `(Forge RPS / Rust RPS - 1) × 100`, p95 감소는 `(1 - Forge p95 / Rust p95) × 100`으로 계산했다. 음수는 각각 처리량 감소 또는 지연 증가를 뜻한다. p95 중앙값은 모든 요청을 합쳐 계산한 p95와 다르다.

| API | 동시 요청 | Rust req/s | Forge req/s | Forge 처리량 변화 | Rust p95 ms | Forge p95 ms | Forge p95 감소 |
|---|---:|---:|---:|---:|---:|---:|---:|
| `/api/health` | 1 | 2,925.04 | 1,458.82 | -50.13% | 0.589 | 1.303 | -121.22% |
| `/api/health` | 16 | 11,379.15 | 9,845.43 | -13.48% | 1.913 | 2.233 | -16.73% |
| `/api/posts` | 16 | 3,683.11 | 5,920.86 | +60.76% | 7.004 | 3.909 | 44.19% |
| `/api/posts` | 64 | 3,599.13 | 5,909.54 | +64.19% | 33.379 | 19.545 | 41.45% |

게시물 목록에서 Forge 처리량은 동시 요청 16/64 기준 +60.76% / +64.19%였고, p95 지연은 44.19% / 41.45% 감소했다. health에서는 Forge 처리량 변화가 동시 요청 1/16 기준 -50.13% / -13.48%였다. 이 결과만으로 모든 API에서 같은 우위를 주장할 수 없다.

5회 측정의 최소–최대와 같은 반복 번호끼리 비교한 RPS 비율은 다음과 같다. 표의 paired 중앙값과 위의 중앙값 비율은 서로 다른 통계다.

| API | 동시 요청 | Rust RPS 범위 | Forge RPS 범위 | paired Forge/Rust 중앙값 (범위) |
|---|---:|---:|---:|---:|
| `/api/health` | 1 | 2,079.17–3,190.13 | 1,243.88–1,761.24 | 0.5700 (0.4825–0.6372) |
| `/api/health` | 16 | 11,160.81–11,472.98 | 9,245.54–9,967.88 | 0.8688 (0.8117–0.8838) |
| `/api/posts` | 16 | 3,528.83–3,697.14 | 5,787.33–5,946.04 | 1.6144 (1.6074–1.6494) |
| `/api/posts` | 64 | 3,516.84–3,672.16 | 5,820.57–6,072.64 | 1.6585 (1.5978–1.6804) |

| API | 동시 요청 | Rust p95 ms 범위 | Forge p95 ms 범위 |
|---|---:|---:|---:|
| `/api/health` | 1 | 0.473–0.752 | 1.050–1.424 |
| `/api/health` | 16 | 1.892–1.939 | 2.156–2.361 |
| `/api/posts` | 16 | 6.883–7.338 | 3.829–4.060 |
| `/api/posts` | 64 | 32.558–34.522 | 19.197–20.090 |

API별 제한은 CPU 2코어와 메모리 512 MiB, DB 연결 풀은 5개다. PostgreSQL 16은 같은 호스트의 독립된 tmpfs 인스턴스에서 애플리케이션별 DB를 제공하며 API의 CPU 제한을 적용하지 않았다. 공개 게시물 100개·댓글 0개·동일한 프로젝트 10개를 사용했다. 댓글 인덱스는 Rust와 Forge DB 모두에 생성했다. 게시물·프로젝트 JSON은 시간 형식을 정규화하고 ID로 정렬한 뒤 일치 여부를 확인했다. 프로젝트는 사전 검증만 수행했고 처리량은 측정하지 않았다.

AMD BC-250, Linux 6.8.0-139-generic 공유 호스트에서 각 조건을 1초 예열하고 4초 동안 요청을 시작했다. 진행 중인 마지막 요청까지 완료한 실제 경과 시간으로 RPS를 계산했다. HTTP/1.1 keepalive와 identity encoding을 사용했고 nginx를 경유하지 않았다. 생성기는 Python/aiohttp이며 동시 요청 1에는 프로세스 1개, 나머지에는 동기화한 프로세스 2개를 사용했다. Rust와 Forge는 동시에 부하를 받지 않으며 반복마다 실행 순서를 바꿨다.

자원 표에서 CPU 100%는 코어 1개다. 각 회차의 CPU 평균을 중앙값으로 요약했고, 메모리는 0.2초 간격으로 샘플링한 cgroup `memory.current`의 회차별 최대값이다. 스레드도 회차별 관측 최대값의 중앙값이다.

| API | 동시 요청 | 구현 | API CPU % | DB CPU % | 생성기 CPU % | 메모리 MiB 중앙값 / 최대 | 스레드 중앙값 |
|---|---:|---|---:|---:|---:|---:|---:|
| `/api/health` | 1 | Rust | 21.06 | 0.01 | 63.21 | 7.539 / 7.879 | 4 |
| `/api/health` | 1 | Forge | 18.84 | 25.58 | 49.62 | 2.383 / 3.289 | 3 |
| `/api/health` | 16 | Rust | 80.99 | 0.35 | 197.85 | 7.617 / 7.723 | 4 |
| `/api/health` | 16 | Forge | 95.52 | 117.19 | 196.70 | 5.246 / 5.258 | 18 |
| `/api/posts` | 16 | Rust | 143.63 | 263.76 | 132.80 | 8.840 / 9.180 | 4 |
| `/api/posts` | 16 | Forge | 109.91 | 417.26 | 184.03 | 5.633 / 5.914 | 18 |
| `/api/posts` | 64 | Rust | 139.02 | 263.89 | 133.50 | 8.762 / 9.242 | 4 |
| `/api/posts` | 64 | Forge | 107.48 | 412.28 | 186.07 | 14.020 / 14.039 | 66 |

게시물 목록의 동시 요청 64에서는 Forge의 API CPU가 Rust보다 낮았지만 DB CPU는 더 높았다. 관측 메모리 중앙값도 Forge 14.020 MiB, Rust 8.762 MiB로 Forge가 더 컸다. 높은 처리량과 함께 DB·메모리 비용이 달라졌으므로 API 컨테이너 CPU만 보고 전체 시스템의 효율이 개선됐다고 판단할 수 없다. health 동시 요청 16에서는 두 구현 모두 생성기 CPU가 약 197%에 도달해 부하 생성기의 처리 한계가 결과에 영향을 줄 수 있다.

API cgroup의 CPU throttling 횟수 합계는 2회다. 두 회는 Rust의 게시물 목록 동시 요청 16·첫 반복에서 발생했고 합계 59.467ms였다. 다른 회차와 Forge에는 관측된 throttling이 없었다. CPU 관측 창은 4.1515–4.1978초이며 준비·결과 수집 시간도 포함한다. 생성기 CPU는 실제 요청 경과 시간을 분모로 사용한다. 분모가 다른 CPU 수치로 요청당 CPU 비용을 직접 계산하지 않았다. 최대 생성기 시작 지연은 0.8216ms다. 샘플링된 메모리는 절대 최고 RSS나 장시간 사용량 상한을 증명하지 않는다.

구현 차이는 다음과 같다. 각 차이가 최종 수치에 기여한 정도는 이번 비교만으로 분리할 수 없다.

| 경로 | Rust | 최적화 Forge |
|---|---|---|
| HTTP | Actix Web, 기본 worker 설정 | libmicrohttpd, connection 모드, auto에서 poll 선택 |
| DB 실행 | bb8-postgres / tokio-postgres 비동기 조회 | 연결 풀 5개, 동기 libpq 준비된 쿼리 |
| 목록 JSON | 조회 행을 PostSummary로 변환 후 serde_json 직렬화 | PostgreSQL json_agg 결과를 응답으로 전달 |
| 차단 확인 | BanStore 메모리 스냅샷, 앱의 변경은 DB와 메모리에 반영 | 공개 조회마다 PostgreSQL의 최신 차단 상태 확인, posts는 목록과 같은 SQL에 포함 |
| health | 캐시 기반 차단 검사 후 JSON 반환 | 차단 검사와 health JSON을 포함한 SQL 실행 |
| 빌드 | Rust 1.89 release, thin LTO, Debian/glibc | GCC 13.2.1 -O2, Alpine/musl, native 의존성 Release |

health의 작업량은 위처럼 다르다. 생성기 CPU가 두 프로세스의 한계에 가까운 조건에서는 서버 처리량 차이가 작게 보일 수 있다. 게시물 목록에서 JSON 작업을 DB로 옮긴 구조의 효과나 언어·컴파일러 자체의 효과를 이 수치로 단독 입증하지 않았다. DB CPU와 생성기 CPU도 API 밖에서 공유 호스트의 자원을 사용한다.

앞서 같은 날 수행한 pool/poll → connection/poll 대조는 계측 Forge 바이너리를 양쪽 모두 사용했다. Rust 비교는 비계측 Forge 바이너리를 사용했으므로 두 실험의 RPS를 직접 섞지 않는다.

| 동시 요청 | Forge 처리량 변화 | p95 감소 | connection 핸들러 중 DB 풀 대기 |
|---|---:|---:|---:|
| 16 | +3.89% | 32.13% | 48.73% |
| 64 | +2.14% | 9.63% | 90.95% |

이 대조는 HTTP 스레드 방식 변경 후 지연 감소를 확인하는 자료다. Rust 대비 전체 차이를 모두 스레드 변경에 귀속할 수 없다. 동시 요청 64에서 계측한 Forge 핸들러 시간의 약 91%는 DB 연결 풀 대기였다. 풀 크기별 비교는 아직 수행하지 않았고, 비동기 DB 처리를 도입했을 때의 효과도 이번 자료에 없다.

댓글 인덱스의 별도 SQL 검증은 게시물 1,000개(공개 20개)·댓글 200,000개로 수행했다. 각 단계 7회 중 첫 회를 제외한 중앙값에서 특정 게시물 댓글 조회는 23.8195 → 2.8080ms로 약 8.48배 빨라졌고 목록 JSON은 37.9880 → 44.4920ms로 느려졌다. 댓글 0개인 HTTP 비교와 데이터가 다르므로 위의 Rust/Forge 차이를 인덱스의 성과로 설명할 수 없다. DB 준비 검사의 TCP 전환은 기동 경합을 해결하며 요청 처리 경로를 변경하지 않는다.

측정용 이미지는 보존된 실행 파일을 사용했다. 이번 비교에서 Rust 또는 Forge 전체를 소스부터 다시 빌드하지 않았다. Forge runtime은 준비된 after-release 이미지 위에 검증된 최종 바이너리와 migration 11을 복사한 이미지다. 별도의 API 통합 26개, migration/restart/index 검증을 통과한 그 실행 파일을 사용했다. 이번 Rust DB에는 비교 도구가 댓글 인덱스를 직접 생성했으므로 Rust의 migration 11 기동 검증을 뜻하지 않는다.

| 식별 항목 | SHA-256 / image ID |
|---|---|
| `rust_image_id` | `sha256:06263bc0a2216ddbdc985d1a728983c4d4b95f494cadb53b0f4f3b957918d0da` |
| `rust_binary_sha256` | `6cd0cdfa8868840ed0b2fe8645be864b3703a857efea4e77b0e43a16566cfe33` |
| `forge_runtime_image_id` | `sha256:f0a497df4f13e616260764e3cfd860fb421e66b98321589fa694365927f02a5a` |
| `forge_binary_sha256` | `0f6a4690708d53db5b43b79ad430c7e1a18f98c17920045f5a7d2962b29ccfc8` |
| `candidate_web_sha256` | `ff7908bd428bfb19aed12dff9cce15523447ec26e83d011817448164207fb3dc` |
| `load_source_sha256` | `56bd71d2fb59f6d0531e813cf07cedc8cf9ce53226568b9e283a91affb1e894d` |
| `postgres_image_id` | `sha256:cf78e76683b9ca8c5733cbbdce6c9262b45b6767934dd0a95e671f9a0fc20685` |

[40회 원본](rust-forge-comparison-2026-10-04.json), [통계 및 SQL 실행 시간](rust-forge-comparison-2026-10-04-summary.json), [20회 동일 poll 대조 원본](rust-forge-comparison-2026-10-04-poll-control.json)을 함께 보관했다. 원본 비교 JSON의 자유 서술 `scope`에는 이전 도구의 'same ban policy' 표현이 남아 있지만, DB 확인과 캐시 확인의 차이는 위의 구현 표처럼 해석해야 한다. 생성기 프로세스 수도 원본의 `client_processes`와 회차별 필드를 따른다.

재현에는 보존 manifest·이미지·바이너리와 aiohttp 환경이 필요하다. 아래 명령은 기존 자료를 덮어쓰지 않는 새 출력 경로를 사용한다.

```sh
/tmp/forge-benchmark-env/bin/python benchmark/portfolio_diagnose.py \
  --prepared /tmp/forge-continuation-20261003/portfolio-comparison-prepared.json \
  --profile-dir /tmp/forge-improvement-20261003/verified-binaries \
  --runtime-image forge-portfolio-improved:1791120438822375152 \
  --output /tmp/forge-rust-comparison-reproduction.json \
  --improvement --client-processes 2 --repeats 5 --seconds 4 \
  --variants rust plain_fixed_connection
```

호스트의 다른 서비스는 실행 중인 상태였다. 5회의 짧은 실험은 운영 최대 용량, 장시간 안정성, 대용량 응답·업로드 성능 또는 다른 데이터에서의 우위를 검증하지 않는다. 배포 조건에 대한 판단에는 대표 데이터, 더 긴 측정 창, DB·생성기 자원 분리와 health 작업량을 맞춘 대조가 필요하다. 이번 실행의 일회용 비교 컨테이너와 네트워크는 완료 후 정리했다.

실시간 DB·차단 검사를 유지한 후속 health 개선은 [API health 최적화 및 Rust 비교](health-optimization-2026-10-04.md)에 기록했다. 이 보고서의 기존 측정값은 그대로 보존했다.
