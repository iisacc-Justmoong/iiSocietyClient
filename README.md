# iiSocietyClient

## Saved host reconnection

The encrypted group-session record retains the current registered host/container
binding and up to eight private LAN address/UDP-port hints. `savedHostConnection()`
returns nothing after account, host or container changes or logout. Routes are
not authentication and are never exposed as credentials through QML.

On restart, `NearbyDevices::resumeHost()` immediately sends an account-signed
five-second discovery challenge to saved routes, alongside ordinary Bonjour/BLE
discovery. The registered host returns its fresh signed discovery record and the
challenge. The caller validates account scope, host identity, source address,
challenge, lifetime and record signature before feeding normal automatic TLS
pairing. The host validates the client's current signed record first. No old QR,
TLS socket, proof or execution nonce is restored; TLS pinning and fresh mutual
account authentication remain mandatory. Probes retry at five-second intervals
and stop when connected, paused or inactive. Old hosts ignore the additional
message and ordinary discovery remains the fallback for changed addresses.
When a verified device returns with a fresh process nonce, stale discovery
routes and any invitation for its old process are retired immediately rather
than waiting for the automatic pairing attempt's 20-second timeout.
Nonces retired by a fresh authenticated resume exchange are ignored in delayed
Bonjour callbacks for 70 seconds (bounded to 128 entries). A fresh resume proof
can reactivate that process; signed but stale discovery alone cannot roll it back.

Tests cover restart persistence, wrong-host rejection, logout, recovery without
Bonjour announcements, and rejection of a saved route with a wrong account key.

Society와 소비 앱이 공유하는 인증·검색·선택 다운로드 세션이다. Society UI 프로세스가 중단되어도 앱 내부에서 같은 App Group의 Society 컨테이너와 인증 세션을 사용한다. 호스트의 확인된 namespace와 기존 TLS/HMAC 페어링을 유지한다.

`Client::start(container)`는 기존 Society replica를 열고 메타데이터와 로컬 제출을 동기화한다. `StorageMap::request(keys)`는 특정 버전을 고정한 다운로드 요청을 디스크에 남긴다. `Client::synchronizeNow()`로 처리를 촉진하고 `StorageMap::requestState(id)`의 ready 이후에만 파일을 사용한다. 취소·실패 시 검증되지 않은 부분 파일은 공개 경로에 나타나지 않는다.

호스트에만 있는 모델의 생성은 `requestHost`로 `society.generation` 요청을 보내고 `hostResponse`를 받는다. 호출자는 주소·자격 증명·다른 호스트를 지정하지 못하며 현재 replica에 연결된 인증 호스트만 사용한다. 모델 다운로드와 별도 요청으로 처리하므로 로컬 모델을 받기 전에 호스트 큐를 사용할 수 있다. 연결 전이나 로그아웃 후에는 요청을 보내지 않는다. LAN TLS와 계정 relay 양쪽을 세션 테스트에서 검증한다.

앱은 생성 결과를 Society 내부에서 atomic rename/commit으로 완성한 뒤 synchronizeNow를 호출한다. 네트워크가 끊기면 원본 파일과 제출 저널이 남고 다음 연결에서 재시도한다. 계정·페어링 상태는 기존 암호화된 GroupSessionStore에만 저장하며 파일 컨테이너에 복사하지 않는다.

AccountSession, NearbyDevices, AutomaticPairing, GroupSessionStore 구현을 Society 앱에서 이 SDK로 이전했다. Society의 QML AccountController는 얇은 등록 래퍼다. 앱 생명주기·백그라운드 실행 허가·상태 표시 정책은 소비 앱이 담당한다.


의존성은 iiSocietySync 0.8.0, iiSocietyContainer 0.14.0, iiServerHost 0.6.0, iiAcountManager 및 기존 Qt이다. 데스크톱 상태 암호화에는 OpenSSL Crypto, iOS에는 CryptoKit을 사용한다. SDK는 상위 Product 소스나 Helper에 의존하지 않는다. 헤더는 `iiSocietyClient/Client.h` 경로로 설치한다. 소비 앱은 저장소 객체를 만들 때 start, 명시 다운로드/파일 공개 후 synchronizeNow, 백그라운드 전환/종료 시 stop을 호출한다.

`AccountSession::storageScope()`는 인증 서비스 origin과 계정 sub로 계산하며 LAN proof와 서버 복제가 같은 namespace를 사용한다. 다른 계정의 replica는 열지 않는다. 최초 Society 설정이 끝나지 않았으면 새 호스트를 임의 선택하지 않는다. `client-session.lock` 점유 실패는 기다렸다가 재시도하고 다른 프로세스의 연결을 강제로 끊지 않는다.

`iiSocietyClient.Session`은 격리된 계정 서비스·저장소와 실제 TLS 또는 loopback relay를 사용한다. Society 앱 프로세스 없이 원본 미보유 목록에서 모델 요청·완료·로컬 결과 업로드·로그아웃 중단을 확인한다. 검색 레코드의 전달은 주입한 DiscoveryService이며 실제 Bluetooth 라디오 성능 검사는 아니다. 테스트는 실사용 계정 복원을 비활성화한다.

SDK 루트의 설치기는 이 디렉터리의 CMake 프로젝트와 의존성을 자동 발견한다. `SDK/install.sh --dry-run --only iiSocietyClient`로 Container·ServerHost·Sync·Account의 설치 순서를 확인할 수 있다. 이번 검증은 macOS와 iOS arm64를 대상으로 하며 Android·Windows의 패키징/기기 실행을 대신하지 않는다.

## Source layout

Implementation files and their headers live together under `src/`. Existing feature and platform subdirectories retain their responsibilities. Build configuration, tests, documentation, resources, and maintenance scripts remain at the project root. Configure and build using the repository-local `build/` directory.

`AccountSession` also enables the account SDK's private Society drive-location synchronization:
after sign-in and every 30 seconds, the current account receives `societyContainerDrive` (disk ID,
host device ID, image path and revision). Other devices retain their local replica/mount paths.
A location refresh does not request pairing keys or emit account changes when the value is unchanged.
The account API and SDK contract are documented in `iiAcountManager/docs/ACCOUNT_CONTRACT.md`.

## 계정 GraphQL 호환성

iiAcountManager 0.2.8 이상이 필요하다. 로그인과 컨테이너 위치 동기화는 해당 SDK의
`POST /Account/GraphQL` `appSession` mutation을 사용한다. 페어링도 같은 endpoint에
`variables.input = {intent: "pairing", device: ...}`를 보낸다. `data.appSession.pairing`을
검증한 뒤에만 저장하며, HTTP 200 GraphQL 오류의 세션 만료도 AccountManager에 전달한다.
쿠키 범위·계정/기기 결합·키 검증·보안 저장소 계약은 유지한다. 운영 GraphQL 서버 배포가
새 클라이언트 배포보다 먼저 이루어져야 하며 REST 재시도는 없다.

## macOS 개발 빌드의 로그인 저장소

실행 중인 프로세스의 Security framework 서명 정보를 확인한다. 개발자 팀이 없는
비샌드박스 실행 파일(임시 서명 포함)은 사용자 Application Support 아래
`iisacc/Development Groups/<SocietyAppGroup>/Library/Application Support/SocietyState`를
사용한다. 앱 이름이 달라도 같은 그룹의 GUI와 daemon은 같은 위치를 사용한다.
개발용 Keychain 서비스도 `.development.encrypted-state`로 분리한다. 기존 AES-256-GCM,
0700/0600 권한, 철회 세대와 암호문 검증은 그대로 적용한다.

팀 서명 또는 sandbox가 있는 앱은 계속 OS App Group API를 사용한다. 서명 조회 실패나
보호 그룹 접근 거부를 개발 경로로 우회하지 않는다. iOS의 App Group 및 Data Protection도
유지한다. 정식 서명과 개발 서명 간 로그인 정보를 자동 복사하지 않는다.
`iiSocietyClient.AppleState`는 실제 임시 서명 프로세스에서 경로 선택과 앱/daemon 일치를
검증한다. 암호화 저장·삭제·재실행 검사는 Society.GroupState에서 수행한다.

LAN clients require server-issued account pairing proofs. Both LAN and relay select the host and container in the authenticated account `societyContainerDrive`; missing registration or another host grants no synchronization. The registered replacement host adopts a fresh mirror while retaining previous files in recovery.

Apple keychain reads, writes and duplicate-item checks execute on worker threads. Completion returns to the session store thread and is cancelled when its QObject is destroyed. Keychain access prompts therefore cannot block the Society or consumer application event loop; existing access controls and encrypted state are preserved.
