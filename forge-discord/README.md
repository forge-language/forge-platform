# Forge Discord community bot

공식 [forge-language/forge-discord](https://github.com/forge-language/forge-discord) 라이브러리를 사용하는 네이티브 Forge 봇입니다. 명령 처리, 권한·역할 서열, 서버별 설정, 반응 역할과 자동 관리 로직은 `src/*.fg`에 있습니다. C 브리지는 HTTP/WebSocket 전송, JSON·버퍼, Unicode 정규화, 시간·신호와 원자적 파일 저장만 제공합니다. 실행 이미지에는 Node.js나 discord.js가 없습니다.

새 봇은 라이브러리의 [`bot`·`slash` 함수 API](../vendor/forge-discord/README.md#함수만으로-시작하기)로 HTTP·JSON 없이 작성할 수 있습니다. 이 커뮤니티 봇은 여러 요청 사이 Gateway 유지·명령 조기 응답·권한 검사에 필요한 기존 문맥을 사용합니다. Serenity와 Twilight에서 참고한 설계와 장단점은 [라이브러리 비교](../vendor/forge-discord/README.md#serenity와-twilight에서-참고한-점)에 있습니다.

## 빌드 및 실행

지원하는 운영 경로는 Linux의 Docker Compose입니다. 저장소의 서브모듈을 초기화하고, Docker·Compose·make를 준비합니다. 호스트에 Forge나 Node.js를 설치할 필요는 없습니다.

```sh
cd forge-discord
# 이미 .env를 작성했다면 덮어쓰지 마세요.
test -f .env || cp .env.example .env
chmod 600 .env
# .env의 DISCORD_TOKEN과 DISCORD_CLIENT_ID에 실제 값 입력
make build
make check
# 모든 설치 서버에서 사용할 전역 명령 등록
make register
make start
make logs
```

테스트 서버에만 즉시 명령을 등록하려면 `make register GUILD=서버ID`를 사용합니다. 예전 서버 전용 명령이 남아 있으면 이 명령으로 갱신하세요. 등록 도구는 이 애플리케이션의 해당 범위 명령을 교체합니다. `make stop`은 봇만 중지하며 설정 볼륨을 삭제하지 않습니다.

`.env`에는 아래 값을 저장합니다. 토큰은 **Bot token**이며 Application ID와는 다른 값입니다. 파일을 셸 스크립트로 실행하지 않습니다.

```dotenv
DISCORD_TOKEN=실제_봇_토큰
DISCORD_CLIENT_ID=실제_애플리케이션_ID
```

Compose는 설정을 전용 `discord-data` 볼륨의 `/app/data/guilds`에 저장합니다. 서버 ID나 `config.json`은 필요하지 않습니다. 재시작·이미지 교체 시 같은 볼륨을 유지하고 백업하세요. 볼륨을 삭제하는 `down -v`는 설정도 지웁니다. 하나의 설정 디렉터리는 한 봇 프로세스만 사용해야 합니다.

직접 네이티브 빌드하려면 WebSocket을 지원하는 libcurl 8.14 이상, OpenSSL, json-c, libmicrohttpd, ICU와 C/CMake 개발 도구가 필요합니다. 저장소 루트에서 `sh forge-discord/scripts/build-native.sh`를 실행하면 `build/forge-discord`, `build/forge-discord-register`, `build/forge-discord-check`를 생성합니다. 실행 작업 디렉터리에 `.env`를 두거나 환경변수를 설정합니다. `BOT_CONFIG_DIR`의 기본값은 `data/guilds`입니다. Alpine 이미지에서 빌드한 바이너리는 해당 musl 런타임이 필요하므로 이미지 안에서 실행합니다.

## Discord 설정

1. [Developer Portal](https://discord.com/developers/applications)에서 봇을 만들고 Bot 토큰과 Application ID를 `.env`에 저장합니다.
2. **Bot → Privileged Gateway Intents → Message Content Intent**를 켜고 저장합니다. 꺼져 있으면 `Used disallowed intents`로 Gateway 연결이 거부됩니다. 전체 멤버 목록은 요청하지 않으므로 Server Members Intent는 필요하지 않습니다.
3. 설치 시 `bot`, `applications.commands` scope를 선택합니다. 사용하는 기능에 맞게 View Channels, Send Messages, Read Message History, Add Reactions, Manage Messages, Moderate Members, Kick Members, Ban Members, Manage Roles 권한을 부여합니다. Administrator 권한은 필요하지 않습니다.
4. 봇 역할을 지급할 역할과 관리할 멤버보다 위에 둡니다. 실제 채널의 권한 덮어쓰기도 확인하세요.
5. 서버 관리 권한이 있는 운영자가 `/setup`으로 설정합니다. 모든 설정 응답은 실행자에게만 보입니다.

## 서버에서 설정

새 서버는 자동 관리가 꺼져 있고 반응 역할과 감사 채널이 없는 상태로 시작합니다. 서버 ID는 명령을 실행한 서버에서 자동으로 정합니다. 설정은 즉시 반영됩니다.

| 명령 | 동작 |
| --- | --- |
| `/setup show` | 현재 설정 확인. 긴 설정은 응답 길이 제한에 맞게 일부만 표시 |
| `/setup log channel:#감사` | 감사 채널 지정. channel 생략 시 해제 |
| `/setup automod enabled:true messages:6 window:5 mentions:8 timeout:300` | 자동 관리 및 한도 지정. 선택값 생략 시 기존 값 유지 |
| `/setup word-add word:금칙어` | 금칙어 추가 |
| `/setup word-remove word:금칙어` | 금칙어 삭제 |
| `/setup reaction-add channel:#역할 message:메시지ID emoji:✅ role:@역할` | 기존 메시지에 반응 역할 연결 |
| `/setup reaction-remove role:@역할` | 연결 해제. 이미 지급된 역할은 유지 |
| `/setup exempt-role role:@역할 exempt:true` | 자동 관리 제외 역할 지정. false로 해제 |
| `/setup exempt-channel channel:#채널 exempt:true` | 자동 관리 제외 채널 지정. false로 해제 |

서버 관리 권한은 실행할 때마다 Discord에서 다시 조회하고 Forge 정책으로 검사합니다. 설정은 `서버ID.json`에 파일 권한 600으로 원자적으로 저장합니다. 저장·검증 실패 시 변경을 성공으로 표시하지 않습니다. 기존 `config.json` 또는 `BOT_CONFIG` 파일이 있으면 해당 서버의 저장된 설정이 없을 때만 처음 읽어 가져옵니다. 이후 설정을 오래된 파일로 덮어쓰지 않습니다. Compose에서 가져오려면 파일을 `/app/config.json`에 읽기 전용 마운트하거나 `BOT_CONFIG`를 별도 마운트 경로로 지정하세요.

## 반응 역할과 관리 명령

역할 안내 메시지를 먼저 작성하고 `/setup reaction-add`로 연결한 뒤 `/roles message:메시지ID`를 실행하면 이모지를 붙입니다. Unicode 이모지는 원문, 커스텀 이모지는 해당 서버의 숫자 emoji ID를 사용합니다. 반응을 추가하면 역할을 지급하고 제거하면 회수합니다. 역할당 연결은 하나입니다.

관리자·서버/역할/채널/웹훅/메시지 관리, 추방·차단·타임아웃·전체 멘션 권한이 있는 역할, 연동 관리 역할, `@everyone`, 봇 이상 서열은 연결하지 않습니다. 봇이 꺼진 동안의 반응은 소급 동기화하지 않습니다. 수동 지급한 같은 역할도 반응 제거 시 회수될 수 있으므로 전용 역할을 사용하세요.

- `/purge count:1..100`: 14일 미만 메시지만 삭제합니다.
- `/timeout member:@멤버 minutes:1..40320 reason:사유`: 멤버를 일시 제한합니다.
- `/kick`, `/ban`: 현재 서버 멤버를 추방·차단합니다. 선택적 reason은 Discord 감사 로그에 전달합니다.

호출자·봇 권한과 역할 서열을 다시 검사합니다. 서버 소유자·자신·봇 대상은 거부하며 호출자와 같거나 높은 역할도 거부합니다. 소유자는 자신의 서열 제한만 우회하며 봇의 서열 제한은 유지됩니다.

## 자동 관리와 운영 경계

금칙어는 NFKC 정규화와 소문자 변환 후 부분 문자열로 검사합니다. 새 메시지와 수정 메시지에 적용하며 우회 표현 전체를 감지하는 AI 필터는 아닙니다. 도배 창은 1~60초, 메시지·멘션 한도는 2~100, 타임아웃은 0~2419200초입니다. `timeout:0`은 메시지만 삭제합니다. 서버 소유자·Manage Messages 운영자·면제 역할/채널·봇은 제외합니다. 스레드는 상위 채널 권한을 상속해 계산합니다.

도배 기록은 제한된 메모리 창에만 유지되며 재시작하면 초기화됩니다. Gateway 이벤트는 순서대로 처리하고 REST 호출 사이에 heartbeat와 새 이벤트를 처리합니다. 인터랙션은 먼저 비공개 지연 응답을 보냅니다. REST와 이벤트 대기열에는 시간·크기 제한이 있어 오류 시 실패를 기록하며 무제한으로 쌓지 않습니다. 감사 채널 메시지는 멘션을 발생시키지 않습니다. 토큰과 메시지 본문은 로그에 쓰지 않습니다. 음성·음악 기능은 포함하지 않습니다.

## 테스트

개발 테스트는 Docker, Node.js 22 이상과 OpenSSL CLI를 사용합니다. Node.js는 가짜 HTTP/WSS 서버와 테스트 실행에만 사용하며 운영 이미지에는 포함하지 않습니다.

```sh
cd forge-discord
make test
```

실제 Forge 바이너리로 설정 검증·저장/복원·권한·역할 서열을 검사합니다. 공식 라이브러리의 오프라인/Gateway 상태 테스트, C 브리지의 HTTP/WSS 분할 프레임·heartbeat 관련 프로토콜과 가짜 Discord HTTP 서버를 통한 명령·반응·자동 관리 통합 테스트도 실행합니다. 데이터는 임시 디렉터리를 사용하고 실제 서버의 메시지나 멤버를 변경하지 않습니다.

실제 자격증명과 Gateway READY 확인은 `docker compose run --rm --no-deps bot forge-discord --gateway-check`로 실행합니다. 명령 등록이나 메시지 전송 없이 연결만 확인합니다. 실제 Discord에서의 역할 추가/반응 제거, 저권한 사용자의 명령 거부, 욕설 수정/도배 삭제와 감사 채널 전달은 별도 테스트 서버에서 확인해야 합니다.

라이브러리는 `vendor/forge-discord` 서브모듈의 고정 커밋을 사용합니다. 라이브러리가 문서화한 확장 SDK 전체 대신 이 봇에서 쓰는 HTTP/WebSocket·버퍼 프리미티브만 로컬 브리지로 제공합니다. 공식 참고: [Gateway](https://docs.discord.com/developers/events/gateway), [권한](https://docs.discord.com/developers/topics/permissions), [명령 등록](https://docs.discord.com/developers/interactions/application-commands).
