# Forge HTTP 벤치마크 성능 분석 보고서

**작성일:** 2026-07-21
**대상:** Forge AOT HTTP 서버 (`benchmark/forge/bench_server.fg`)
**비교 대상:** Python 3 (stdlib socket), Phoenix (Bandit), Rust (Axum release)

---

## 1. 요약

Forge는 동일 조건의 HTTP 마이크로벤치마크에서 **Python 대비 약 1.65배**, **Phoenix 대비 약 1.26배**, **Rust Axum 대비 약 1.05배** 높은 처리량을 기록했다. 동시에 **메모리 사용량은 2.4 MB**로 가장 낮고, CPU 사용 효율도 우수했다.

| 구현 | Requests/sec | Peak RSS | Avg CPU (16 cores) | 에러 |
|------|-------------|----------|-------------------|------|
| **Forge** | **13,386** | **2.4 MB** | 27.2% (1.7% of capacity) | 0 |
| Rust Axum | 12,810 | 5.5 MB | 62.9% (3.9%) | 0 |
| Phoenix (Bandit) | 10,600 | 117.0 MB | 447.0% (27.9%) | 0 |
| Python 3 (socket) | 8,119 | 11.3 MB | 14.4% (0.9%) | 0 |

**핵심 결론:** Forge의 높은 성능은 인터프리터나 JIT이 아니라 **AOT 네이티브 컴파일 + 최소 syscall 경로 + 멀티코어 REUSEPORT epoll** 조합에서 나온다. 벤치마크 워크로드(고정 13바이트 응답, `Connection: close`)에 맞춰 스택을 최적화한 결과이며, 복잡한 라우팅·JSON·TLS가 있는 실서비스와는 다를 수 있다.

---

## 2. 벤치마크 방법론

### 2.1 환경

| 항목 | 값 |
|------|-----|
| OS | Linux 7.0.0-27-generic x86_64 |
| CPU | 16 logical cores |
| 날짜 | 2026-07-21 12:02 UTC |
| 서버 affinity | `taskset -c 0-15` (전체 코어) |

### 2.2 부하 조건

- **요청 수:** 1,000,000
- **동시 연결:** 1,000
- **HTTP 메서드:** `GET /`
- **응답 본문:** `Hello, World` (13 bytes)
- **연결 정책:** `Connection: close` (요청당 accept → respond → close)

### 2.3 측정 항목

`benchmark/run_benchmark.sh`가 다음을 기록한다.

- **Throughput:** requests/sec
- **Peak RSS:** 서버 프로세스 트리의 최대 상주 메모리 (MB)
- **Avg / Peak CPU:** `ps %cpu` 기준 (멀티스레드 합산, 100% 초과 가능)

원시 로그: `benchmark/results.txt`

---

## 3. Forge 벤치마크 서버 구조

벤치마크 서버는 9줄의 Forge 소스로 구성된다.

```forge
import http;

native main {
    let server: int = http_listen(19080);
    http_prepare(server, "Hello, World");
    http_serve_mt(server, 0);
}
```

실제 I/O는 C stdlib (`stdlib/http.c`, `stdlib/tcp.c`)에서 처리된다. Forge 컴파일러는 `.fg`를 C로 변환한 뒤 **네이티브 바이너리**로 링크하므로, 런타임에 VM·GC·JIT 오버헤드가 없다.

### 3.1 요청 처리 파이프라인

```
[클라이언트 connect]
    → epoll (listen fd readable)
    → accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)
    → recv (헤더 1회 discard, 스택 버퍼 512B)
    → send (사전 빌드된 응답 버퍼 1회)
    → close
```

클라이언트 fd에 대한 epoll 등록, HTTP 파싱, 라우팅, 동적 응답 생성이 **없다**. 이 워크로드에 맞는 최단 경로다.

---

## 4. 성능이 좋은 이유 (기술 분석)

### 4.1 AOT 네이티브 컴파일

| | Forge | Python | Phoenix | Rust |
|--|-------|--------|---------|------|
| 실행 모델 | AOT → 기계어 | 인터프리터 + C 확장 | BEAM VM | AOT (LLVM) |
| 요청 핫패스 | C stdlib 직접 호출 | Python 루프 + syscall | Plug 파이프라인 | Tokio + Hyper |

Forge의 `bench_server.fg`는 컴파일 후 **일반 C HTTP 서버와 동일한 수준**으로 동작한다. 언어 차이는 초기화 코드에만 국한되고, 100만 요청 처리 중 대부분의 시간은 `serve_client()` 같은 C 함수에서 소비된다.

### 4.2 사전 빌드된 응답 (`http_prepare`)

`http_prepare()`는 서버 기동 시 한 번만 HTTP 응답 전체를 힙에 생성한다.

```c
// stdlib/http.c — 응답을 시작 시 1회 생성
snprintf(out, cap,
         "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%.*s",
         blen, (int)blen, body);
```

요청마다 `snprintf`, `strlen`, 헤더 조립이 발생하지 않는다. Python 벤치마크도 고정 바이트열을 쓰지만, Forge는 **단일 `send()` 버퍼**로 헤더+본문을 한 번에 전송한다.

### 4.3 Linux fast path: accept4 + 즉시 응답

```c
// runtime/platform.c
return (int)accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

// stdlib/http.c — serve_client
fr_sock_recv(client, discard, sizeof(discard));   // 1 syscall
fr_sock_send(client, srv->cached_resp, srv->cached_len);  // 1 syscall
fr_sock_close(client);
```

- **accept4:** `accept` + `fcntl` 비용 절감
- **클라이언트 epoll 미사용:** 짧은 연결에서는 등록/해제 비용이 이득보다 큼
- **TCP_NODELAY 생략:** localhost 고정 소량 응답에서 Nagle 이점 없음, `setsockopt` syscall 제거

요청당 핵심 syscall: **recv + send + close** (accept는 listen 스레드에서).

### 4.4 멀티코어 REUSEPORT + epoll

`http_serve_mt(server, 0)`은 CPU 코어 수만큼 REUSEPORT accept worker를 띄운다.

```c
int accept_workers = (int)(threads > 0 ? threads : cpus);
// 코어당 1 worker, fr_thread_pin_cpu()로 CPU 고정
```

각 worker는 독립 epoll 루프를 가진다.

```
Core 0: [epoll] → accept → serve_client (same thread)
Core 1: [epoll] → accept → serve_client (same thread)
...
Core 15: [epoll] → accept → serve_client (same thread)
```

**커널이 연결을 코어별 listen 소켓에 분산** (`SO_REUSEPORT`)하고, accept 스레드가 곧바로 응답하므로 **mutex/cond 큐 handoff가 없다**. 이전 설계(accept worker → connection pool → serve worker)에서 제거한 것이 Axum과 구조적으로 유사해지며, 처리량이 크게 올랐다.

소켓 튜닝 (`stdlib/tcp.c`):

- `listen(backlog=4096)`
- `SO_RCVBUF` / `SO_SNDBUF` = 64 KiB

### 4.5 낮은 메모리 사용 (2.4 MB Peak RSS)

| 요인 | 설명 |
|------|------|
| 정적 링크 최소 런타임 | BEAM/ Tokio 런타임 없음 |
| per-request heap 할당 없음 | 응답 버퍼·discard 버퍼 모두 스택 또는 기동 시 1회 할당 |
| 스레드 수 = 코어 수 | Phoenix(수백 MB) 대비 스레드·힙 구조 단순 |

Python(11.3 MB)보다도 작은 이유는 **멀티스레드 accept worker**가 있어도 프로세스 전체 RSS가 작은 네이티브 바이너리이기 때문이다.

### 4.6 CPU 효율 (27.2% avg vs Axum 62.9%)

Forge는 동일 throughput에서 **더 적은 CPU**를 쓴다. 원인:

1. **동기식 최단 경로** — async 상태 머신·태스크 웨이크업 비용 없음
2. **핸드오프 제거** — 스레드 간 큐잉 없음
3. **프레임워크 레이어 없음** — Hyper/Plug 라우터·미들웨어 스택 없음

Phoenix가 447% CPU를 쓰는 것은 BEAM 스케줄러·가비지 컬렉션·프로세스 트리 때문이며, throughput은 Forge보다 낮다.

---

## 5. 경쟁 구현과의 비교

### 5.1 vs Python 3 (stdlib socket)

Python도 고정 응답·단순 루프를 쓰지만:

- 바이트코드 인터프리터 오버헤드
- GIL로 인한 멀티코어 활용 제한
- 객체 할당·참조 카운팅

→ **약 65% 수준의 throughput** (8,119 vs 13,386 req/s)

### 5.2 vs Phoenix (Bandit)

Phoenix는 production-grade HTTP 스택(Plug, Bandit, Thousand Island)을 포함한다.

- 라우팅·미들웨어·슈퍼비전 트리
- BEAM 프로세스 모델
- JIT이 아닌 VM 실행

벤치마크 서버는 최소 라우터지만 프레임워크 고정 비용이 남는다. → **메모리 117 MB, CPU 447%**로 높은 리소스 대비 **10,600 req/s**.

### 5.3 vs Rust Axum

Axum은 Forge와 가장 유사한 대역(12,810 vs 13,386 req/s, 약 4.5% 차이)이다.

| | Forge | Axum |
|--|-------|------|
| 동시성 | 코어당 1 blocking worker + epoll | Tokio 멀티스레드 + async |
| 최적화 | `-O3`, 선택적 LTO | release + `lto=true` |
| HTTP 스택 | raw socket + cached response | Hyper (성숙한 파서/버퍼링) |

Forge가 소폭 앞선 이유는 이 워크로드에서 **async 런타임 오버헤드가 없고 경로가 더 짧기** 때문이다. 반면 복잡한 핸들러·스트리밍·미들웨어에서는 Axum/Hyper가 유지보수·기능 면에서 유리할 수 있다.

---

## 6. 적용된 최적화 이력

초기 Forge 벤치마크(~12,600 req/s) 이후 다음 변경으로 **~13,400 req/s**까지 개선되었다.

| 변경 | 효과 |
|------|------|
| Connection pool 제거 (accept 스레드에서 직접 `serve_client`) | mutex/cond handoff 제거, oversubscription 감소 |
| `TCP_NODELAY` fast path 제거 | 연결당 `setsockopt` 1회 절감 |
| accept worker = CPU 코어 수 (1:1) | 캐시 친화적, 컨텍스트 스위칭 감소 |
| CPU pinning (`fr_thread_pin_cpu`) | NUMA/캐시 미스 완화 |
| LTO 옵션 (`FORGE_ENABLE_LTO`) | 네이티브 링크 최적화 (환경별) |

### 제거된 병목 (초기 설계)

| 병목 | 영향 |
|------|------|
| 요청마다 `malloc`/`free` (헤더 버퍼) | 힙 churn |
| `send()` 2회 (헤더 + 본문 분리) | 추가 syscall |
| recv until `\r\n\r\n` 루프 | 추가 syscall |
| 요청마다 `snprintf` 응답 생성 | CPU |
| accept → serve 스레드 풀 handoff | mutex/cond, 288 OS 스레드 |

---

## 7. 한계와 주의사항

1. **마이크로벤치마크:** 고정 응답·단일 경로·TLS 없음. 실제 API 서버 결과와 다를 수 있다.
2. **localhost:** 네트워크 RTT 없음. WAN 환경에서는 다른 병목이 지배적이다.
3. **측정 변동:** README 기준 run-to-run **±5%** 변동 가능.
4. **CPU % 해석:** `ps %cpu`는 멀티스레드 합산이며, 코어 수로 나눈 "capacity %"는 참고 지표다.
5. **복잡한 워크로드:** DB 조회·JSON 직렬화·WebSocket 등에서는 언어/프레임워크 생태계가 더 중요해진다.

---

## 8. 재현 방법

```bash
cmake --build build --target bench_server
./benchmark/run_benchmark.sh
```

포트 19080–19083이 사용 중이면:

```bash
fuser -k 19080/tcp 19081/tcp 19082/tcp 19083/tcp
```

결과는 `benchmark/results.txt`에 저장된다 (CPU/RAM 메트릭 포함).

---

## 9. 결론

Forge HTTP 벤치마크의 높은 성능은 다음 공식으로 요약할 수 있다.

> **AOT 네이티브 코드** + **사전 빌드 응답** + **accept4 즉시 응답** + **REUSEPORT 멀티코어 epoll** + **핸드오프 없는 동기 경로**

이 조합은 "Hello, World" 수준의 초고속 HTTP 서버에 최적화되어 있으며, Forge 언어 자체가 Python/Elixir보다 빠른 것이 아니라 **컴파일 결과물이 최소 C HTTP 서버와 동등한 핫패스**를 갖기 때문이다. 동시에 Rust Axum과 대등한 수준까지 끌어올린 것은 아키텍처 단순화(connection pool 제거, 코어 affinity)의 효과가 크다.

향후 개선 여지: ~~`io_uring`, `sendfile`, TLS termination, 복잡한 라우팅 벤치마크 추가~~ → **구현 완료** (아래 §10 참고).

---

## 10. 고급 HTTP 벤치마크 (io_uring / sendfile / TLS / routing)

### API

| 함수 | 설명 |
|------|------|
| `http_prepare_sendfile` | memfd + `sendfile()` zero-copy 응답 준비 (Linux) |
| `http_serve_uring` | liburing 기반 accept/recv/send (Linux, liburing 필요) |
| `http_listen_tls` / `http_serve_tls_mt` | OpenSSL TLS termination |
| `http_serve_routing_mt` | 9개 JSON 라우트 내장 MT 벤치 서버 |

### 실행

```bash
cmake --build build --target benchmarks
./benchmark/run_advanced_benchmark.sh
```

포트: 19080 (epoll baseline), 19086 (sendfile), 19087 (io_uring), 19088 (TLS), 19089 (routing)

결과: `benchmark/advanced_results.txt`

CMake 옵션: `FORGE_ENABLE_IO_URING`, `FORGE_ENABLE_TLS` (기본 ON)

---

*관련 파일:* `benchmark/forge/bench_server.fg`, `stdlib/http.c`, `stdlib/tcp.c`, `benchmark/run_benchmark.sh`
