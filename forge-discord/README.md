# forge-discord

Forge 커뮤니티의 Discord 관리 봇입니다. `/purge`, `/timeout`, `/kick`, `/ban`,
이모지 반응 역할 지급/회수, 욕설·도배·과도한 멘션 관리와 선택적 감사 채널을 제공합니다.
명령 권한·역할 서열·반응 역할 허용·자동 관리 판단은 `src/policy.fg`에서 실행합니다.
TypeScript는 Discord Gateway/REST, 설정 파일, 이벤트 수명과 짧은 도배 기록을 연결합니다.

## 실행

Node.js 22 이상을 권장합니다 (최소 20.19). Forge 컴파일러가 필요합니다.
`FORGE=/절대/경로/forge`로 컴파일러 경로를 지정할 수 있으며 기본값은 `~/.forge/bin/forge`입니다.

```sh
cd forge-discord
npm ci
cp .env.example .env
cp config.example.json config.json
# .env에 봇 토큰/Application ID, config.json에 실제 서버/채널/메시지/역할 ID 입력
npm test
npm run check-config
npm run register
npm start
```

`.env`와 실제 `config.json`은 Git에 넣지 않습니다. 토큰은 Discord Developer Portal의
**Bot token**을 사용하며 사용자 계정 토큰은 사용하지 않습니다. 토큰은 빌드 결과에 포함되지 않습니다.
서버 ID는 초대 링크에서 얻을 수 없으므로 Discord 개발자 모드를 켜서 서버 ID를 복사합니다.
등록은 이 애플리케이션의 지정된 서버 명령만 갱신합니다.

## Discord 설정

1. [Developer Portal](https://discord.com/developers/applications)에서 `forge-discord` 애플리케이션/봇을 만듭니다.
2. Bot → Privileged Gateway Intents에서 **Message Content Intent**를 켭니다.
   이 구현은 `Guilds`, `GuildMessages`, `GuildMessageReactions`, `MessageContent` 인텐트를 사용합니다.
   전체 멤버 목록을 수신하지 않으므로 Server Members Intent는 필요하지 않습니다.
3. OAuth2 URL Generator에서 `bot`과 `applications.commands` scope를 선택합니다.
   필요한 권한은 **View Channels, Send Messages, Read Message History, Add Reactions,
   Manage Messages, Moderate Members, Kick Members, Ban Members, Manage Roles**입니다.
   Administrator 권한은 필요하지 않습니다. 사용하는 채널에서 View/Send/Read 권한을 허용합니다.
4. 봇 역할을 지급할 역할 및 관리할 멤버 역할보다 위로 이동합니다.
   명령은 호출자 권한과 봇 권한을 실행 시 다시 확인합니다. 서버 소유자·자신·봇 대상은 거부하며,
   호출자와 같거나 높은 역할도 거부합니다 (소유자는 자신의 서열 제한만 우회).
5. 감사 기록을 원하면 `logChannelId`를 지정합니다. 필요 없으면 이 키를 삭제합니다.

## 반응 역할

1. 일반 Discord 메시지로 역할 안내를 작성하고 메시지/채널 ID를 복사합니다.
2. `config.json`의 `reactionRoles`에 채널 ID, 메시지 ID, 이모지, 역할 ID를 등록합니다.
   Unicode 이모지는 `✅`처럼 원문 그대로 넣습니다. 커스텀 이모지는 숫자 emoji ID를 넣습니다.
3. 봇을 재시작하고 `/roles message:메시지ID`를 실행하면 등록된 이모지를 붙입니다.
4. 멤버가 이모지를 누르면 역할을 지급하고, 반응을 제거하면 해당 역할을 회수합니다.

역할 하나당 반응 연결 하나만 허용합니다. 관리자·서버/역할/채널/웹훅/메시지 관리,
추방·차단·타임아웃·전체 멘션 권한이 있는 역할, 연동 관리 역할, `@everyone`, 봇 이상 서열은 거부합니다.
기존 메시지도 재시작 뒤 새 반응 이벤트를 처리합니다. 봇이 꺼진 동안 발생한 반응은 소급 동기화하지 않습니다.
반응으로 연결한 역할은 봇이 소유하는 용도로 사용하세요. 수동으로 받은 같은 역할도 반응 제거 시 회수됩니다.
서버 관리 권한이 있는 운영자만 `/roles`를 실행할 수 있습니다.

## 자동 관리

`blockedTerms`는 NFKC 정규화와 대소문자 통일 후 **부분 문자열**로 검사합니다.
기본 한국어/영어 예시는 서버 규칙에 맞게 수정하세요. 우회 표현 전체를 감지하는 AI 필터는 아닙니다.
신규 메시지와 수정된 메시지를 검사하고, 창당 메시지 수와 고유 사용자/역할 멘션 수를 제한합니다.
`@everyone`/`@here`는 멘션 한도 초과로 취급합니다.
위반 메시지를 삭제하고 `timeoutSeconds > 0`이면 권한·서열이 허용하는 멤버를 제한합니다.
`0`이면 삭제만 합니다. 서버 소유자, Manage Messages 운영자, 면제 역할/채널, 봇은 제외합니다.
`enabled: false`이면 자동 관리를 끕니다. 도배 기록은 메모리에만 유지되며 재시작 시 초기화됩니다.

`/purge count:1..100`은 14일 미만 메시지만 삭제합니다. `/timeout`은 1..40320분입니다.
`/kick`·`/ban`은 현재 서버 멤버가 대상이며 선택적 `reason`을 Discord 감사 로그에 남깁니다.
자동 관리 오류 및 실행 기록은 stdout/stderr에 기록하며 메시지 본문이나 토큰을 로그에 쓰지 않습니다.

## 운영 및 검증 범위

Gateway 봇은 계속 실행되는 Node 프로세스이므로 systemd, 컨테이너 또는 VM 등에서 한 인스턴스로 실행합니다.
일반 Cloudflare Workers fetch 핸들러로 직접 옮기는 구성은 아닙니다.
`SIGTERM`/`SIGINT`에서 Gateway 연결을 종료합니다. 호스트 서비스 관리자로 재시작을 설정하세요.

`npm test`는 실제 Forge 컴파일 결과의 권한/서열, 반응 역할 제한, 욕설/도배 경계와 설정을 검증합니다.
SDK 대역을 사용해 명령 실행·역할 메시지 준비, 반응 지급/회수 순서와 API 실패 복구,
신규/수정 메시지 삭제·타임아웃도 검증합니다.
실제 서버 연결은 별도의 봇 토큰과 서버 설정 후 확인해야 합니다. 배포 전 테스트 서버에서
저권한 계정의 명령 거부, 역할 추가/반응 제거, 욕설 수정/도배 삭제, 감사 채널을 확인하세요.

공식 참고: [Gateway 인텐트](https://docs.discord.com/developers/events/gateway),
[Discord 권한](https://docs.discord.com/developers/topics/permissions).
