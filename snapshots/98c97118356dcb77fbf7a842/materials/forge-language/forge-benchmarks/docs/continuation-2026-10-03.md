# Forge 최적화·셀프 호스팅·Rust 비교 보고서 (2026-10-03)

기준 소스는 `ba36611`의 Git archive이며, 이후 소스는 이번 작업의 수정본입니다. 원시 측정값과 바이너리·라이브러리 SHA256은 [런타임 증거](continuation-runtime-comparison.json), [문자열 증거](continuation-string-comparison.json)에 보관합니다. 이번 측정은 기존 서비스가 함께 실행되는 공유 호스트에서 진행했으며, 측정 중에는 빌드나 다른 벤치마크를 실행하지 않았습니다.

## 구현 및 검증

- 스케줄러가 활성 코루틴과 대기 I/O 수를 상태 변경 때 갱신합니다. 처리 여부를 판단할 때 완료된 코루틴 목록 전체를 매번 탐색하던 비용을 제거했습니다. 상태 변경과 집계는 기존 스케줄러 잠금 아래에서 처리합니다.
- 문자열 뷰 기반 부분 문자열, 범위 비교, 빌더 범위 복사를 C/JS 백엔드와 FG 컴파일러에 연결했습니다. 범위 처리·빌더 복사와 파일 쓰기를 공통 함수로 정리했습니다.
- 기본 `forge 파일.fg` 네이티브 출력, 설치 경로 탐색, 입력 덮어쓰기 방지, 파일 쓰기 오류 처리, 플랫폼 링크 옵션과 정적 라이브러리 순환 참조를 처리했습니다. C/FG 컴파일러 버전은 0.3.0입니다.
- `bootstrap/compiler.fg`가 옵션 처리, 검증, 코드 생성 및 argv 기반 C 컴파일러 호출을 수행합니다. 같은 줄의 println 뒤 문장 누락, 복합 대입, 숫자 문자열 API 반환값 출력 오류를 수정했습니다.
- Release 전체 빌드 및 CTest 12개 스위트 통과: 네이티브 컴파일러 17개, 드라이버 5개, FG 컴파일러 14개 실제 실행 검사 등을 포함합니다. stage3/stage4 생성 C의 바이트 동일성을 확인했습니다. 문자열, OS, 스케줄러는 ASan/UBSan 검사도 통과했습니다.
- 실제 cmake 설치본을 프로젝트 밖에서 호출해 C/FG 컴파일·실행을 확인했습니다. 사용자 `~/.local`에도 설치하여 현재 PATH에서 `forge`, `forge-fg`를 사용할 수 있습니다.
- `forge-language:local` Docker 이미지 빌드와 이미지 내부 hello/thread/control_flow 실행을 확인했습니다. 이미지 빌드 자체가 FG 고정점 검증을 수행합니다. 자세한 사용법은 [컴파일러 안내](compiler-and-selfhosting.md)에 있습니다.

Linux에서 검증했습니다. Windows 파일 식별과 macOS 실행 경로 처리도 수정했지만 해당 OS에서 실행한 결과는 아닙니다. FG 컴파일러는 지원 부분집합을 셀프 호스팅하며 전체 C 컴파일러의 모듈·타입·소유권 기능과 동일하지 않습니다. 최종 기계어 생성과 런타임은 C 도구 체인을 사용합니다.

## 런타임 및 FG 컴파일러

호스트는 Linux 6.8.0-139 / x86_64 / glibc 2.39이며 컴파일러는 Ubuntu GCC 13.3.0입니다. 동일한 Release 설정(-O3, NDEBUG, GPU OFF, LTO OFF)과 동일한 벤치마크 C 소스를 사용했습니다. 기준/이후 순서를 번갈아 7회 측정한 중앙값입니다. dispatch는 사전 생성된 코루틴을 실행하며 spawn/해제 비용을 제외하고 worker 시작/종료를 포함합니다. 각 코루틴은 한 번 yield하고 완료하며, yield는 reduction budget 안에서 처리되므로 매번 큐에 재삽입하는 부하가 아닙니다.

| 부하 | 이전 wall ms | 이후 wall ms | 이전/이후 | 이전 CPU ms | 이후 CPU ms |
|---|---:|---:|---:|---:|---:|
| 10,000 코루틴 / 1 worker | 61.504 | 7.480 | 8.22× | 66.726 | 12.327 |
| 100,000 코루틴 / 4 workers | 3533.036 | 106.924 | 33.04× | 3600.946 | 381.245 |
| 500ms I/O 대기 | 500.698 | 500.773 | 1.00× | 3.381 | 3.419 |
| 공통 FG 컴파일러 원본 → C | 67.320 | 18.872 | 3.57× | — | — |

10만 코루틴 wall 범위는 이전 2357.269–6215.195ms, 이후 88.632–118.622ms입니다. 이전 결과의 변동이 크므로 중앙값 배수를 다른 부하나 모든 프로그램에 적용할 수 없습니다. 유휴 대기 wall/CPU에는 유의미한 개선을 주장하지 않습니다.

FG 변환은 동일한 39,699바이트 기준 원본을 입력합니다. C 컴파일 시간은 제외하고 프로세스 실행·C 파일 생성 시간을 포함합니다. 새 헤더와 오류 처리로 생성 C는 이전/이후가 다를 수 있습니다. 양쪽 결과를 C로 컴파일해 같은 네이티브 예제를 다시 컴파일·실행하는 것을 확인했고, 각 변형의 출력은 반복마다 동일했습니다. 이는 공통 예제의 동작 확인이며 전체 언어 의미의 동등성 증명은 아닙니다.

## 문자열 알고리즘

이번 라이브러리의 기존 부분 문자열 경로와 새 뷰 경로를 같은 바이너리에서 7회 비교했습니다. 기준 버전과의 비교가 아닌 API/알고리즘 비교입니다. 기존→신규 순서는 고정되어 있고 짧은 측정은 캐시·스케줄링 영향을 받습니다. view 생성과 builder finish를 포함하고 arena reset은 제외합니다. 비교는 4,096번의 8바이트 일치 검사, 복사는 32바이트 조각의 전체 문자열 재구성입니다. 결과 바이트와 체크섬을 검증했습니다.

| 작업 | 입력 bytes | 부분 문자열 ms | 뷰 ms | 비율 |
|---|---:|---:|---:|---:|
| 범위 빌더 복사 | 4,096 | 0.020796 | 0.003621 | 5.74× |
| 범위 빌더 복사 | 16,384 | 0.080920 | 0.003928 | 20.60× |
| 범위 빌더 복사 | 65,536 | 1.302285 | 0.018845 | 69.11× |
| 범위 일치 | 4,096 | 0.519385 | 0.075301 | 6.90× |
| 범위 일치 | 16,384 | 0.604609 | 0.027300 | 22.15× |
| 범위 일치 | 65,536 | 2.550536 | 0.027943 | 91.28× |

과거에 추가한 view scan/builder append 측정도 원시 JSON에 유지했습니다. 그 큰 배수는 기존 불변 문자열 반복 복사의 제곱 비용을 피하는 알고리즘 차이이며 이번 변경의 새 성능 개선으로 계산하지 않습니다.

## Portfolio HTTP / 이전 Rust 프로젝트

측정 대상은 이전에 만든 portfolio-platform의 Rust Actix 백엔드와 Forge 백엔드입니다. 두 Forge 빌드는 동일한 애플리케이션·네이티브 어댑터·PostgreSQL/Web vendor 스냅샷을 사용하고, 기준/이후 언어 소스만 바꿨습니다. 애플리케이션의 기존 최소 CMake 패키징으로 compiler/include/runtime/stdlib를 다시 빌드했습니다. 기존 오래된 성능 이미지나 배포 이미지를 재사용하지 않았습니다. 실제 이미지 ID와 소스 SHA256은 [HTTP 원시 결과](continuation-portfolio-comparison.json)에 있습니다.

각 서버는 2 CPU quota, 512 MiB, DB 연결 5개로 제한했습니다. Forge는 MHD worker 8개이며 Rust는 Actix 자동 worker 설정을 유지합니다. 격리된 tmpfs PostgreSQL과 동일한 100개 게시글 및 프로젝트 데이터를 사용했습니다. Forge 이전/이후 health/posts/projects JSON은 일치했고, Rust posts/projects는 모든 필드를 시간 표기 정규화와 ID 정렬 후 비교했습니다. health 응답 형태는 Rust/Forge가 다릅니다.

3개 API × 동시 요청 1/16/64 × 3개 서버 × 3회로 총 81개 측정 구간을 수행했습니다. 각 구간은 warmup 1초 + 측정 5초이며 순서를 회전합니다. 측정 구간 요청 1,602,084개, 오류 0개입니다. 다음은 중앙값이며 p95는 각 구간 p95의 중앙값입니다.

| API | 동시 요청 | 이전 req/s | 이후 req/s | Rust req/s | 이후/이전 | 이후/Rust | 이후 p95 ms | Rust p95 ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| /api/health | 1 | 1561.30 | 1578.67 | 2244.48 | 1.011× | 0.703× | 1.040 | 0.678 |
| /api/health | 16 | 6292.89 | 6343.35 | 7181.11 | 1.008× | 0.883× | 3.632 | 2.935 |
| /api/health | 64 | 6657.01 | 6444.08 | 7248.55 | 0.968× | 0.889× | 13.360 | 13.083 |
| /api/posts | 1 | 529.40 | 530.46 | 420.58 | 1.002× | 1.261× | 3.350 | 4.492 |
| /api/posts | 16 | 4388.37 | 3039.66 | 3666.53 | 0.693× | 0.829× | 9.109 | 6.964 |
| /api/posts | 64 | 5269.30 | 4726.38 | 3613.11 | 0.897× | 1.308× | 26.912 | 33.783 |
| /api/projects | 1 | 775.03 | 736.46 | 406.05 | 0.950× | 1.814× | 2.668 | 3.206 |
| /api/projects | 16 | 4995.55 | 4713.82 | 5344.23 | 0.944× | 0.882× | 5.876 | 4.399 |
| /api/projects | 64 | 5915.98 | 5840.68 | 5534.89 | 0.987× | 1.055× | 17.537 | 18.329 |

이 측정에서 HTTP 전체 개선을 확인하지 못했습니다. 게시글 동시 요청 16개 구간은 이후 중앙값이 30.7% 낮았고 64개 구간도 10.3% 낮았습니다. 전자를 작거나 무의미한 차이로 처리하지 않고 동일 이미지의 추가 5회 측정으로 점검합니다. 공유 호스트의 변동과 부하 생성기·DB 영향을 분리할 수 없으므로 원인을 단정하지 않습니다.

Rust와의 비교는 API와 동시 요청 수준에 따라 우열이 달라집니다. Forge는 `/api/health` 세 구간에서 Rust보다 낮은 처리량을 보였습니다. 게시글 1/64 및 프로젝트 1/64 구간의 높은 비율을 전체 언어 성능으로 일반화하지 않습니다.

64개 동시 요청에서 서버 cgroup CPU/메모리 중앙값은 다음과 같습니다. CPU 100%는 한 코어에 해당하며 메모리는 memory.current로 캐시·매핑 등을 포함합니다. DB/부하 생성기 CPU는 이 값에 포함되지 않습니다.

| API | 이전 CPU % | 이후 CPU % | Rust CPU % | 이전 MiB | 이후 MiB | Rust MiB |
|---|---:|---:|---:|---:|---:|---:|
| /api/health | 58.78 | 58.05 | 53.27 | 10.98 | 10.91 | 6.08 |
| /api/posts | 83.41 | 73.30 | 150.63 | 11.50 | 11.56 | 8.66 |
| /api/projects | 113.28 | 112.26 | 175.89 | 12.39 | 12.33 | 8.78 |

서버 경로는 libmicrohttpd worker → Forge handler → 동기 libpq이며 코루틴 dispatch 벤치마크 경로와 다릅니다. Rust는 Debian/glibc + Rust 1.89 release이고 Forge는 Alpine/musl + GCC 네이티브 코드입니다. Rust의 ban cache와 Forge의 즉시 PostgreSQL ban 검사, 자동 Actix worker 수와 고정 MHD worker 수도 다릅니다. 따라서 이 결과는 두 애플리케이션 구현의 비교입니다. Python aiohttp 부하 생성기도 빠른 경로의 처리량 상한에 영향을 줄 수 있습니다.

### 게시글 동시 요청 16개 추가 점검

[추가 원시 결과](continuation-portfolio-posts16-followup.json)는 같은 이미지·같은 자원 제한으로 게시글/동시 요청 16개만 5회 회전 측정합니다. 사전 동등성 검사는 health/posts/projects 전체에서 다시 통과했습니다. 15개 측정 구간 모두 오류 0입니다.

| 서버 | req/s 중앙값 | req/s 최소–최대 | p95 중앙값 ms | CPU 중앙값 % | MiB 중앙값 |
|---|---:|---:|---:|---:|---:|
| forge_before | 4168.84 | 2570.92–4805.74 | 6.731 | 68.05 | 5.44 |
| forge_after | 3664.60 | 2028.89–4668.76 | 9.121 | 59.91 | 5.25 |
| rust | 3699.37 | 3667.34–3720.42 | 6.743 | 149.28 | 6.34 |

추가 중앙값은 이후/이전 0.879×로 12.1% 낮으며 p95도 35.5% 높습니다. 최초의 30.7% 감소는 다시 같은 크기로 나타나지 않았지만 성능 문제가 없다고 결론 내릴 수 없습니다. 반복별 이후/이전 비율은 1.108, 0.487, 1.579, 0.971, 0.789로 우열과 크기가 불안정합니다. 이 공유 환경에서 HTTP 개선을 주장하지 않으며 감소 원인은 미확정입니다.

두 컴파일러가 생성한 서버 C는 바이트 단위로 같았습니다(SHA256 `3d76c53441a44f1eb1814bf09221787268187433c3499f400c23aad63ddaba76`). public SQL 조합에 사용하는 str_concat와 arena 구현은 이번 변경에서 수정하지 않았습니다. 새 view/builder 경로는 이 API에서 호출하지 않습니다. 이 관찰은 컴파일러의 서버 코드 생성 변경을 제외하는 근거이며, 런타임 배치·DB·부하 생성기·호스트 변동 중 어느 것이 원인인지 증명하지 않습니다.

HTTP 측정 뒤 새 OS API의 문자열 반환 분류 누락을 최종 수정했습니다. 측정된 compiler snapshot SHA는 당시 값 그대로 보존합니다. 이 API는 portfolio 서버에서 호출하지 않으며 수정 후의 서버 C도 같은 SHA256으로 확인했습니다. 런타임 측정에서 사용한 stage2 바이너리도 최종 바이너리와 같은 SHA256입니다. stage0 forge 바이너리는 이 분류 수정으로 바뀌었으며 최종 SHA는 이미지 증거 JSON에 별도로 기록했습니다. 런타임·표준 라이브러리 소스는 측정 이후 바뀌지 않았습니다.

## 저장소 정리와 실행 환경

portfolio-platform에는 이 작업에서 루트 `.gitignore`와 `.dockerignore`만 변경했습니다. Python bytecode/cache, Erlang crash dump, Docker context의 로컬 에이전트 상태를 제외했습니다. 기존 build/dist/node_modules/target 규칙을 확인했고 필요한 src/native/vendor/toolchain, 테스트, lockfile, migrations, 자산과 기존 보고서는 유지했습니다. 기존 사용자 수정 사항은 유지했습니다. 새 벤치마크 스테이징·로그·중간 바이너리는 `/tmp`에 두고 보고서에 필요한 원시 JSON과 문서만 Forge 저장소에 보관합니다.

빌드 중 디스크 공간 부족으로 임시 파일 및 apt 검증이 실패했습니다. 이번 작업이 만든 완료된 빌드 단계와 실패한 컨테이너만 정확한 ID로 제거하고 다시 빌드·검증했습니다. 기존 이미지·서비스·볼륨에 대한 전체 prune이나 운영 배포는 수행하지 않았습니다. 두 HTTP 실행은 자신이 만든 컨테이너와 네트워크를 정리했습니다.

최종 네이티브 이미지의 ID/크기/소스 지문은 [이미지 증거](continuation-docker-image.json)에 있습니다. 이미지는 로컬 `forge-language:local` 태그로 제공하며 레지스트리에 게시한 것은 아닙니다.

요청한 OMO 실행은 실제 CLI/plugin으로 시도했고 크레딧 갱신 이후 재시도도 했으나 공급자가 `402 in_flight_budget_exhausted`를 반복 반환했습니다. 사용 가능한 Codex 하위 에이전트 3개를 활용해 문자열/FG 컴파일러, 런타임/OS 검토, Rust 비교·정리를 병행했습니다. 실패한 OMO 실행을 완료된 OMO 작업으로 계산하지 않습니다.

## 재현

다음에서 BEFORE_SRC는 `GIT_MASTER=1 git archive ba36611`로 추출한 소스 디렉터리이며 BEFORE_BUILD/AFTER_BUILD는 각각 Release·GPU OFF로 빌드한 경로입니다. SDK 빌드와 모든 검사 후, 측정 중 다른 빌드/벤치마크를 실행하지 않습니다.

```bash
cmake --build "$BEFORE_BUILD" --target forge-selfhost scheduler_bench
cmake --build "$AFTER_BUILD" --target all forge-selfhost-verify string_bench scheduler_bench
ctest --test-dir "$AFTER_BUILD" --output-on-failure
python3 benchmark/continuation_bench.py --before-root "$BEFORE_SRC" --before-build "$BEFORE_BUILD" --after-root "$PWD" --after-build "$AFTER_BUILD" --output /tmp/runtime.json --repeats 7
python3 benchmark/string_measure.py --binary "$AFTER_BUILD/bin/string_bench" --output /tmp/strings.json --repeats 7
python3 benchmark/portfolio_compare.py --project-root "$PORTFOLIO" --before-root "$BEFORE_SRC" --after-root "$PWD" --output /tmp/portfolio.json --python "$AIOHTTP_PYTHON" --repeats 3 --seconds 5 --build-only
python3 benchmark/portfolio_compare.py --project-root "$PORTFOLIO" --before-root "$BEFORE_SRC" --after-root "$PWD" --output /tmp/portfolio.json --python "$AIOHTTP_PYTHON" --repeats 3 --seconds 5 --resume
```

Portfolio runner는 기존 이름이나 이미지를 덮어쓰지 않고 충돌하면 거절합니다. 비교 프로젝트에는 backend, backend-forge/vendor/toolchain, 해당 격리 harness가 필요하며 Python 환경에는 aiohttp가 필요합니다. 추가 점검은 같은 prepared manifest와 `--clients 16 --endpoints /api/posts --repeats 5`를 사용합니다. 자세한 필터/manifest 옵션은 runner의 `--help`에 있습니다.

전체 타입 검사, 정적 소유권 검증, supervisor 자동 복구, FG 컴파일러의 전체 언어 기능 일치는 아직 후속 작업입니다. 이번에 검증한 것은 위 명시된 지원 기능과 동작이며 모든 언어 기능의 완성이 아닙니다.
