# API health 최적화 및 Rust 비교 — 2026-10-04

측정은 `2026-10-04T14:37:42Z`에 완료했고 보고서는 2026-10-05에 정리했다. 실시간 PostgreSQL DB·IP 차단 검사를 유지한 채 동시 health 요청의 조회를 묶었다. 구성 4개 × 동시 요청 조건 2개 × 5회, 총 40회·957,861건에서 부하 요청 오류는 0건이었다.

동시 요청 16에서 비계측 Forge의 처리량 중앙값은 +5.63%, p95 지연은 4.71% 감소, DB CPU 사용률은 48.35% 감소했다. 단일 요청에서는 처리량 중앙값이 -9.44% 낮았고 속도 향상을 확인하지 못했다. 공유 호스트의 짧은 5회 실험이므로 이 차이를 보편적인 개선이나 확정적인 회귀로 해석하지 않는다.

비관리자 `GET /api/health`는 현재 DB의 차단·만료 상태를 확인한다. 하나의 조회가 실행되는 동안 들어온 요청을 FIFO로 모아 다음 조회에서 최대 64개 IP를 각각 판정한다. 단일 요청은 준비된 EXISTS 조회를 사용한다. 별도 캐시나 묶음을 기다리는 타이머는 없다. 기존 DB 풀 5개를 공유하고 health 조회는 한 묶음씩 실행한다. 이전처럼 정상 200, 차단 403, 조회 오류 500, 연결 확보 실패 503을 반환하고 JSON·CORS·유효한 관리자 우회를 유지한다.

대기열에 남아 있는 요청은 10초 뒤 503으로 종료한다. 이미 조회 대상으로 선택한 요청은 SQL과 연결 정리가 끝난 뒤 반환한다. 따라서 10초가 모든 DB 작업이나 종료 처리의 상한을 의미하지 않는다. 묶음 전체가 DB 오류로 실패하면 현재 대기 요청도 실패로 종료하고, 이후 요청은 새 DB 조회를 시도한다. 다른 공개 API의 조회 동작은 유지했다.

처리량과 p95는 회차별 수치의 중앙값이다. 변화율은 중앙값끼리 비교한다. 계측 바이너리의 처리량은 아래 비계측 비교에 섞지 않는다.

| 동시 요청 | 구현 | req/s 중앙값 | req/s 최소–최대 | p95 ms 중앙값 |
|---|---|---:|---:|---:|
| 1 | Rust | 2,456.89 | 1,738.69–2,987.57 | 0.655 |
| 1 | Forge 개선 전 | 1,374.82 | 1,101.29–1,732.02 | 1.363 |
| 1 | Forge 개선 후 | 1,245.09 | 1,020.68–1,486.63 | 1.375 |
| 16 | Rust | 11,511.45 | 11,031.27–12,561.37 | 1.949 |
| 16 | Forge 개선 전 | 9,632.44 | 9,124.19–9,937.42 | 2.249 |
| 16 | Forge 개선 후 | 10,175.16 | 9,389.85–10,433.77 | 2.143 |

단일 요청의 개선 전·후 p95는 1.363 → 1.375ms로 약 0.88% 늘었고 RPS 범위가 크게 겹쳤다. 동시 요청 16의 최종 Forge 처리량은 Rust보다 11.61% 낮았다. Rust는 BanStore 메모리 스냅샷을 확인하고 Forge는 DB 최신 상태를 조회하므로 이 차이는 언어 자체의 성능 비교가 아니다.

| 동시 요청 | 구현 | API CPU % | DB CPU % | 생성기 CPU % | 관측 메모리 MiB |
|---|---|---:|---:|---:|---:|
| 1 | Rust | 23.07 | 0.06 | 59.50 | 5.484 |
| 1 | Forge 개선 전 | 19.25 | 28.45 | 46.50 | 2.387 |
| 1 | Forge 개선 후 | 19.06 | 23.11 | 51.25 | 2.348 |
| 16 | Rust | 82.85 | 0.03 | 197.63 | 5.566 |
| 16 | Forge 개선 전 | 91.14 | 115.32 | 196.93 | 5.262 |
| 16 | Forge 개선 후 | 98.86 | 59.56 | 197.55 | 5.617 |

CPU 100%는 코어 1개다. CPU는 회차별 평균의 중앙값이며 메모리는 0.2초 간격으로 샘플링한 cgroup memory.current 최대값의 중앙값이다. 관측 메모리는 절대 최고 RSS나 장시간 사용량 상한을 뜻하지 않는다. 동시 요청 16에서 개선 후 API CPU와 메모리는 조금 증가했고 DB CPU는 크게 감소했다. API CPU만으로 전체 효율을 판단하지 않는다. 생성기 CPU가 약 197%로 두 프로세스의 처리 한계에 가까워 처리량 증가를 제한할 수 있다.

별도 계측 빌드에서는 동시 요청 16의 SQL 1회당 처리 요청 수 중앙값이 1.78였다. 단일 요청은 SQL 1회당 요청 1건이었다. 계측 단일 요청의 서버 카운터는 5회 모두 부하 생성기보다 1건 많았다. 이미지의 10초 간격 Docker health probe가 활성화돼 있어 별도 probe 유입과 일치하지만, 요청 출처를 분리 계측하지 않았다. 동시 요청 16에서는 서버 카운터와 생성기 요청 수가 모든 회차에서 일치했고 worker 요청 합계도 서버 카운터와 일치했다. SQL당 요청 수는 서버 카운터를 사용했다. 이 추가 요청을 부하 생성기 처리량에 더하지 않았다.

환경은 AMD BC-250 공유 호스트, Linux 6.8.0-139-generic, API별 2 CPU·512 MiB, PostgreSQL 16 일회용 tmpfs 인스턴스, DB 풀 5개다. 같은 인스턴스의 앱별 DB에 동일한 데이터를 준비했다. 각 조건은 1초 예열 후 4초 동안 요청을 시작하고 마지막 응답까지의 실제 경과 시간으로 RPS를 계산했다. HTTP keepalive·identity encoding을 사용하고 nginx를 경유하지 않았다. 동시 요청 1은 생성기 1개, 동시 요청 16은 동기화한 생성기 2개를 사용했다. 4개 구성은 순차 실행하며 반복마다 순서를 바꿨다.

최종 native 코드·Forge route를 새로 컴파일해 runtime 이미지에 넣었다. Rust release와 비교 전 Forge 실행 파일은 기존 검증 산출물을 사용했다. 전체 Docker 다단계 빌드를 다시 수행한 결과는 아니다. 측정 바이너리 SHA와 검증 바이너리 SHA가 일치하고 현재 소스 SHA도 빌드 스냅샷과 일치한다.

| 검증 | 결과 |
|---|---|
| Native CTest | 3/3 통과, C 컴파일 경고 0 |
| API 통합 | 28/28 통과; 차단·JWT·관리자·CORS·DB 오류·혼합 IP·만료 변경 포함 |
| DB 기동 및 SQL | TCP 준비 확인, migration 11, 재기동, 댓글 인덱스·SQL 검증 통과 |
| Proxy 보안 | nginx 설정 2개, 검증 12개 통과 |
| Health 생명주기 | 대기 80건의 10초 503, 실행 중 연결 종료 및 복구, DB 세션 강제 종료 후 복구, SIGTERM 중 종료 검증 4개 통과 |

코드는 별도 앱 작업 디렉터리 `/home/helloworld0822/orca/workspaces/portfolio-platform/forge-migration/backend-forge`의 `native/adapter.c`, `src/routes.fg`, `tests/integration.py`, `tests/health_batch_lifecycle.py`에 있다. 통합 실행 스크립트에 생명주기 검증을 연결하고 README에 동작을 기록했다. 운영 배포는 수행하지 않았다.

| 식별 항목 | SHA-256 / image ID |
|---|---|
| `portfolio-fixed-profile` | `cb1891ab12fa51a96a57201c618f7dac4dbf89b700c1b38589418a2a5be3df23` |
| `portfolio-fixed` | `cd99f8b1c598993284e53f5ddfb108db7b85757b736fcb3f140f8e4505d264e1` |
| `portfolio-baseline` | `0f6a4690708d53db5b43b79ad430c7e1a18f98c17920045f5a7d2962b29ccfc8` |
| runtime image | `sha256:b1ca3fd38a5652a5a09a55c2f41a8fa56025651a0decea11ff159c6a500544c4` |
| `backend-forge/native/adapter.c` | `dcb2713ca82a5746429e1e761b0fa0b2db6bc4cc92ad91aa97cfd71cd10c3442` |
| `backend-forge/src/routes.fg` | `44cd02a8eeac35ec317253cc83f892d686c1b3c3604ca13731bcabfb2e0c06e0` |

[40회 측정 원본](health-optimization-2026-10-04.json), [계산값·검증 로그·식별값](health-optimization-2026-10-04-summary.json), [이전 게시물·Rust 비교 보고서](rust-forge-comparison-2026-10-04.md)를 함께 보관했다. 이전 보고서의 health 수치는 개선 전 별도 측정이며 이번 수치로 소급 변경하지 않았다. 게시물 성능은 이번 health 실험에서 재측정하지 않았다.

재현에는 보존된 manifest·이미지·바이너리와 aiohttp 환경이 필요하다. 아래 명령은 새 출력 파일을 사용한다.

```sh
/tmp/forge-benchmark-env/bin/python benchmark/portfolio_diagnose.py \
  --prepared /tmp/forge-continuation-20261003/portfolio-comparison-prepared.json \
  --profile-dir /tmp/forge-health-final-20261004 \
  --runtime-image forge-portfolio-improved:1791124204160237120 \
  --output /tmp/forge-health-reproduction.json \
  --improvement --health-only --client-processes 2 --repeats 5 --seconds 4 \
  --variants rust plain_baseline_connection plain_fixed_connection profile_fixed_connection
```

이번 결과는 동시 health 요청의 DB 비용 감소와 해당 조건에서의 처리량 개선을 보여 준다. 단일 요청 속도 향상, 운영 최대 처리량, 장시간 안정성 또는 다른 데이터에서의 같은 효과는 입증하지 않았다.
