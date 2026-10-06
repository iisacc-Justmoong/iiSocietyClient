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

This is the authentication, search, and select download session shared by Society and the consumer app. Society UI process interruption still allows the app to use the Society container and authentication session within the same App Group. It maintains the host's verified namespace and existing TLS / HMAC pairing.

`Client::start(container)` opens the existing Society replica and synchronizes metadata and local submissions. `StorageMap::request(keys)` leaves a fixed-version download request on the disk. Processing is facilitated by `Client::synchronizeNow()` and files are used only after `StorageMap::requestState(id)` is ready. Unverified partial files do not appear in the public path upon cancellation or failure.

Model generation on the host sends a `society.generation` request via `requestHost` and receives `hostResponse`. The caller cannot specify address, credentials, or other hosts and uses only the authentication host connected to the current replica. Since model download and separate requests are handled separately, the host queue can be used before receiving the local model. Requests are not sent before connection or after logout. LAN validates both TLS and account relay in session tests.

The app completes the generation result with an atomic rename/commit inside Society and then calls synchronizeNow. If the network is interrupted, the original file and submission journal remain, and it retries on the next connection. Account and pairing status are stored only in the existing encrypted GroupSessionStore and are not copied to the file container.

Implementation of AccountSession, NearbyDevices, AutomaticPairing, and GroupSessionStore was migrated from the Society app to this SDK. Society's QML AccountController is a thin registration wrapper. The consumer app is responsible for app lifecycle, background execution permission, and status display policy.


Dependencies are iiSocietySync, 0.8.0, iiSocietyContainer, 0.14.0, iiServerHost, 0.6.0, iiAcountManager, and existing Qt. Desktop state encryption uses OpenSSL Crypto and iOS uses CryptoKit. The SDK does not depend on higher-level Product sources or Helpers. Headers are installed via the `iiSocietyClient/Client.h` path. The consumer app calls start when creating the repository object, explicitly downloads/file publishes after synchronizeNow, and calls stop on background transition/termination.

`AccountSession::storageScope()` is calculated using the authentication service origin and account sub, and LAN proof and server replication use the same namespace. Replicas of other accounts are not opened. If the initial Society configuration is not complete, a new host is not arbitrarily selected. `client-session.lock` occupancy failure is retried after waiting, and connections from other processes are not forcibly terminated.

`iiSocietyClient.Session` uses an isolated account service and repository, along with actual TLS or loopback relay. Without the Society app process, it checks model request, completion, local result upload, and logout cancellation from the missing original list. Delivery of search records is via injected DiscoveryService and is not an actual Bluetooth radio performance check. Testing disables real-use account restoration.

SDK's installer automatically discovers the CMake project and dependencies in this directory. The installation order of Container, ServerHost, Sync, and Account can be verified with `SDK/install.sh --dry-run --only iiSocietyClient`. This validation targets macOS and iOS arm64, and does not substitute for Android ·Windows packaging or device execution.

## Source layout

Implementation files and their headers live together under `src/`. Existing feature and platform subdirectories retain their responsibilities. Build configuration, tests, documentation, resources, and maintenance scripts remain at the project root. Configure and build using the repository-local `build/` directory.

`AccountSession` also enables the account SDK's private Society drive-location synchronization:
after sign-in and every 30 seconds, the current account receives `societyContainerDrive` (disk ID,
host device ID, image path and revision). Other devices retain their local replica/mount paths.
A location refresh does not request pairing keys or emit account changes when the value is unchanged.
The account API and SDK contract are documented in `iiAcountManager/docs/ACCOUNT_CONTRACT.md`.

<a id="계정-graphql-호환성"></a>

## Account GraphQL compatibility

iiAcountManager 0.2.8 or higher is required. Login and container location synchronization use the `POST /Account/GraphQL` `appSession` mutation of the corresponding SDK. Pairing also sends `variables.input = {intent: "pairing", device: ...}` to the same endpoint. Storage occurs only after `data.appSession.pairing` is verified, and session expiration for HTTP 200 GraphQL errors is also passed to AccountManager. Cookie scope, account/device binding, key verification, and secure storage contract are maintained. Operational GraphQL server deployment must occur before new client deployment, and REST retry is not performed.

<a id="macos-개발-빌드의-로그인-저장소"></a>

## macOS development build login repository

Checks the Security framework signature information of the running process. Non-sandbox executables without a developer team (including temporary signatures) use `iisacc/Development Groups/<SocietyAppGroup>/Library/Application Support/SocietyState` under User Application Support. GUIs and daemons in the same group use the same location even if the app name differs. The development Keychain service is also separated at `.development.encrypted-state`. Existing AES-256-GCM, 0700/0600 permissions, revocation generations, and ciphertext verification are applied as is.

Apps with team signatures or sandbox continue to use OS App Group API. Failure to retrieve signatures or protection group access denial is not bypassed via the development path. iOS's App Group and Data Protection are also maintained. Login information is not automatically copied between formal and development signatures. `iiSocietyClient.AppleState` validates path selection and app/daemon matching in the actual temporary signature process. Encryption storage, deletion, and re-execution checks are performed at Society. GroupState.

LAN clients require server-issued account pairing proofs. Both LAN and relay select the host and container in the authenticated account `societyContainerDrive`; missing registration or another host grants no synchronization. The registered replacement host adopts a fresh mirror while retaining previous files in recovery.

Apple keychain reads, writes and duplicate-item checks execute on worker threads. Completion returns to the session store thread and is cancelled when its QObject is destroyed. Keychain access prompts therefore cannot block the Society or consumer application event loop; existing access controls and encrypted state are preserved.
