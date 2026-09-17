# iiSocietyClient

Society와 소비 앱이 공유하는 인증·검색·선택 다운로드 세션이다. Society UI 프로세스가 중단되어도 앱 내부에서 같은 App Group의 Society 컨테이너와 인증 세션을 사용한다. 호스트의 확인된 namespace와 기존 TLS/HMAC 페어링을 유지한다.

`Client::start(container)`는 기존 Society replica를 열고 메타데이터와 로컬 제출을 동기화한다. `StorageMap::request(keys)`는 특정 버전을 고정한 다운로드 요청을 디스크에 남긴다. `Client::synchronizeNow()`로 처리를 촉진하고 `StorageMap::requestState(id)`의 ready 이후에만 파일을 사용한다. 취소·실패 시 검증되지 않은 부분 파일은 공개 경로에 나타나지 않는다.

앱은 생성 결과를 Society 내부에서 atomic rename/commit으로 완성한 뒤 synchronizeNow를 호출한다. 네트워크가 끊기면 원본 파일과 제출 저널이 남고 다음 연결에서 재시도한다. 계정·페어링 상태는 기존 암호화된 GroupSessionStore에만 저장하며 파일 컨테이너에 복사하지 않는다.

AccountSession, NearbyDevices, AutomaticPairing, GroupSessionStore 구현을 Society 앱에서 이 SDK로 이전했다. Society의 QML AccountController는 얇은 등록 래퍼다. 앱 생명주기·백그라운드 실행 허가·상태 표시 정책은 소비 앱이 담당한다.


의존성은 iiSocietySync 0.8.0, iiSocietyContainer 0.14.0, iiServerHost 0.6.0, iiAcountManager 및 기존 Qt이다. 데스크톱 상태 암호화에는 OpenSSL Crypto, iOS에는 CryptoKit을 사용한다. SDK는 상위 Product 소스나 Helper에 의존하지 않는다. 헤더는 `iiSocietyClient/Client.h` 경로로 설치한다. 소비 앱은 저장소 객체를 만들 때 start, 명시 다운로드/파일 공개 후 synchronizeNow, 백그라운드 전환/종료 시 stop을 호출한다.

`AccountSession::storageScope()`는 인증 서비스 origin과 계정 sub로 계산하며 LAN proof와 서버 복제가 같은 namespace를 사용한다. 다른 계정의 replica는 열지 않는다. 최초 Society 설정이 끝나지 않았으면 새 호스트를 임의 선택하지 않는다. `client-session.lock` 점유 실패는 기다렸다가 재시도하고 다른 프로세스의 연결을 강제로 끊지 않는다.

`iiSocietyClient.Session`은 격리된 계정 서비스·저장소와 실제 TLS 또는 loopback relay를 사용한다. Society 앱 프로세스 없이 원본 미보유 목록에서 모델 요청·완료·로컬 결과 업로드·로그아웃 중단을 확인한다. 검색 레코드의 전달은 주입한 DiscoveryService이며 실제 Bluetooth 라디오 성능 검사는 아니다. 테스트는 실사용 계정 복원을 비활성화한다.

SDK 루트의 설치기는 이 디렉터리의 CMake 프로젝트와 의존성을 자동 발견한다. `SDK/install.sh --dry-run --only iiSocietyClient`로 Container·ServerHost·Sync·Account의 설치 순서를 확인할 수 있다. 이번 검증은 macOS와 iOS arm64를 대상으로 하며 Android·Windows의 패키징/기기 실행을 대신하지 않는다.
