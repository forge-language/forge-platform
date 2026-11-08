# FG 셀프 호스팅 컴파일러 최적화 및 실행 성능 — 2026-10-05

FG 컴파일러의 내장 함수 이름 변환에서 식별자마다 최대 61개 문자열을 비교하던 비용을 줄였다. 최단 내장 함수 이름보다 짧은 식별자는 바로 반환하고, 앞 두 바이트로 후보 그룹을 고른 뒤 기존 이름을 정확히 비교한다. 61개 이름·반환 매핑은 모두 유지했다. 변경은 `bootstrap/compiler.fg`에 한정되며 C stage0, 런타임, API 코드를 수정하지 않았다.

현재 56,468바이트 원본의 C 변환에서 stage2 wall 중앙값은 17.037 → 13.316ms로 **21.84% 감소**했고, 자식 프로세스 CPU는 16.804 → 12.908ms로 **23.18% 감소**했다. 공통 기준 원본에서는 wall 14.46%, CPU 14.96% 감소했다. 각각 31회 측정한 중앙값끼리 비교했다. 파일 쓰기를 한 번에 모으는 후보도 시험했지만 개선을 확인하지 못해 적용하지 않았다.

## 같은 입력의 stage0·stage2 비교

| 입력 | 컴파일러 | wall 중앙값 ms | CPU 중앙값 ms |
|---|---|---:|---:|
| 공통 기준 원본 39,699 bytes | 역사적 stage0 (C) | 9.695 | 9.396 |
| 공통 기준 원본 39,699 bytes | 역사적 stage2 (FG) | 63.596 | 63.194 |
| 공통 기준 원본 39,699 bytes | 현재 stage0 (C) | 9.350 | 8.841 |
| 공통 기준 원본 39,699 bytes | 개선 전 stage2 (FG) | 14.618 | 14.114 |
| 공통 기준 원본 39,699 bytes | 개선 후 stage2 (FG) | 12.505 | 12.002 |
| 현재 원본 56,468 bytes | 현재 stage0 (C) | 8.467 | 7.919 |
| 현재 원본 56,468 bytes | 개선 전 stage2 (FG) | 17.037 | 16.804 |
| 현재 원본 56,468 bytes | 개선 후 stage2 (FG) | 13.316 | 12.908 |

역사적 기준은 `ba36611`에서 보존한 실행 파일이며 개선 전 FG 소스는 `c6921d7`이다. 현재 stage0는 새 Release 빌드 결과가 이전 최종 바이너리 SHA와 일치했다. 현재 stage2는 이후 제어 흐름·문자열 수정이 포함되어 이전 보고서의 stage2와 다른 실행 파일이다. 역사적 비교에는 모두 처리할 수 있는 같은 39,699바이트 원본을 사용했다. 현재 56,468바이트 원본은 현재 stage0와 개선 전·후 stage2만 비교했다.

현재 원본에서 개선 후 FG stage2도 C stage0보다 wall 1.57배 느렸다. FG 컴파일러가 C 컴파일러와 동등한 속도에 도달했다는 결과는 아니다. 역사적 stage2의 공통 입력 wall 63.596ms에 비해 개선 후는 12.505ms였지만 이 전체 차이에는 이전 문자열 뷰·빌더 등의 변경도 포함된다. 이번 식별자 변환 변경만의 효과는 위의 개선 전·후 비교다.

`--emit-c` 또는 `.c` 출력으로 프로세스 시작, Forge의 검증·파싱·C 파일 쓰기를 측정했다. GCC의 기계어 생성, 링크, 전체 CMake 빌드는 포함하지 않았다. CPU는 자식의 user+system CPU이며 wall에는 스케줄 대기가 포함된다. 실행 순서는 반복마다 정방향·역방향을 번갈았다. 각 변형의 출력은 31회 모두 결정적이었고 개선 전·후 stage2의 C는 두 입력 모두 바이트 단위로 같았다.

## 생성 프로그램과 Rust 실행 비교

같은 정수 점화식 `value = (value * 33 + i) % 1000000007`을 실행 시 인수 개수 × 2,000,000회 계산했다. 이번 실행은 인수 개수 1이며 모든 구현이 `574158008`을 출력했다. 값은 0 이상 1,000,000,007 미만이고 중간 곱셈·덧셈은 33,002,000,200 미만이라 signed 64-bit 범위 안이다. 결과 출력으로 계산을 보존하고 실행 시 입력으로 반복 수를 정했다.

| 생성 도구 | wall 중앙값 ms | CPU 중앙값 ms | wall 최소–최대 ms |
|---|---:|---:|---:|
| 역사적 Forge stage0 | 28.377 | 27.888 | 9.286–32.035 |
| 역사적 Forge stage2 | 28.063 | 27.482 | 9.339–31.327 |
| 현재 Forge stage0 | 20.513 | 19.705 | 9.282–30.481 |
| 개선 전 Forge stage2 | 28.753 | 28.263 | 9.669–30.973 |
| 개선 후 Forge stage2 | 25.360 | 24.754 | 9.216–31.438 |
| Rust release | 28.580 | 28.004 | 10.235–32.077 |

6개 구성의 순서를 회전해 각각 15회, 총 90회 실행했다. C는 GCC 13.3.0의 `-std=c11 -O3`와 같은 현재 Forge 런타임·표준 라이브러리를 사용했다. Rust는 `rustc 1.98.1 (48a229cea 2026-09-01)`의 `-C opt-level=3 -C codegen-units=1`이다. 생성 프로그램의 시작·인수 처리·출력은 포함하고 컴파일 시간은 제외했다. API 비교에 사용한 컨테이너의 Rust 버전·libc와 이 호스트 측정은 다르다.

범위가 크게 겹치고 같은 C를 생성하는 개선 전·후에도 중앙값 차이가 있어, 이 짧은 공유 호스트 실험으로 언어의 실행 성능 우위를 주장하지 않는다. 이번 변경 전·후 stage2가 만든 점화식 C는 바이트 단위로 같았다. 따라서 컴파일러의 이름 조회 비용을 줄인 변경이 이 프로그램의 기계어 생성을 바꿨다는 근거가 없다. 한 정수 계산 예제로 문자열·I/O·코루틴·API 전체 성능을 입증하지 않는다.

## 정확성 및 산출물

- FG 컴파일러 실제 실행 검사 **20/20 통과**. 기존 기능과 새 내장 함수 접두부·짧은 사용자 함수 이름 검사를 포함한다.
- stage2 → stage3 → stage4 생성 C가 모두 바이트 단위로 같은 고정점 검증 통과.
- 공통 입력·현재 입력·실행 예제의 개선 전·후 C 출력 동일성 확인.
- Native/Rust 실행 90회 모두 성공하고 출력 동일.
- Python 구문 검사와 git diff 공백 검사 통과.
- 기존 `build/` 설정(Release·GPU ON·LTO OFF)을 유지하며 `forge-selfhost`, 예제 2개, `forge-selfhost-verify` 갱신 완료. 이 작업 디렉터리의 stage2로도 20/20 실행 검사 통과. 측정 빌드와 생성 C·실행 파일의 `.text`는 동일하다.

고정점은 [문서화한 FG 부분집합](compiler-and-selfhosting.md)의 셀프 호스팅 검증이다. 전체 C 컴파일러의 모듈·타입·소유권 기능 동등성을 증명하지 않는다. 이번 작업은 Linux/glibc 공유 호스트에서 수행했으며 API 부하·빌드는 측정 동안 중단했다. 기존 호스트 서비스와 CPU 주파수·스케줄링의 영향은 남는다. 전체 배포 이미지나 설치본 갱신, 운영 배포는 하지 않았다.

| 식별 항목 | SHA-256 |
|---|---|
| 개선 전 `compiler.fg` | `8332ac161a2fabffe1850a635bb195286d4cc145e84ca137e96fbd0bbfafb6e6` |
| 개선 후 `compiler.fg` | `6dce1375f6bc9f6c2c8857727081d40efc46efc3a8f3dfcb3ca92f5894413b1b` |
| 현재 stage0 | `b87c67f6fb654527115a1a054ed0e0b4d1b40af0c3c2a798ca3b19b1b0c89708` |
| 개선 전 stage2 | `c6e1871c4e12bfb7a169e1ef8a8ba0e6b3a1036bbe6b95304be1529b57cfb8e2` |
| 개선 후 stage2 | `6252a91d9160b6603ca99b46e313a69ecc34c7b87045f736aecb1eb580b8dff6` |
| 갱신한 `build/bin/forge-stage2` | `79a47b07585c179ffd26c302f4dba1b5140da7caed8dfe50b2914fc7e0768c82` |
| stage2/3/4 생성 C | `ffb3d5e4c02578799ba90588194fe54dd4e642d81399f919cebf92885b9c28d4` |

[공통 입력 155회 원본](selfhosting-performance-2026-10-05.json), [현재 입력 93회 원본](selfhosting-performance-2026-10-05-current-input.json), [Forge·Rust 실행 90회 원본](selfhosting-performance-2026-10-05-runtime.json), [소스·라이브러리 지문 및 검증 로그](selfhosting-performance-2026-10-05-validation.json)을 보관했다. API의 Rust 비교와 DB CPU 개선은 [DB CPU 보고서](db-cpu-optimization-2026-10-05.md)에 정리했다.

보존된 실행 파일·입력이 있을 때 C 변환만 다음과 같이 재측정할 수 있다. 출력 위치는 새 파일을 사용한다.

```sh
TMPDIR=/dev/shm python3 benchmark/selfhost_compare.py \
  --stage0 current_stage0 /dev/shm/forge-compiler-cpu-20261005/current-build/bin/forge \
  --stage2 current_stage2 /dev/shm/forge-compiler-cpu-20261005/current-stage2 \
  --stage2 optimized_stage2 /dev/shm/forge-compiler-cpu-20261005/dispatch-stage2 \
  --source /dev/shm/forge-compiler-cpu-20261005/current-compiler.fg \
  --repeats 31 --output /dev/shm/forge-selfhost-reproduction.json
```

실행 비교의 FG와 Rust 원본은 원시 실행 JSON에 전문과 해시를 함께 기록했다. 일회용 빌드·소스·검증 로그는 `/dev/shm/forge-compiler-cpu-20261005`에 있으며 별도 측정 때문에 기존 보고서의 수치를 소급 변경하지 않았다.
