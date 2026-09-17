#include "Client.h"
#include "AccountSession.h"
#include "NearbyDevices.h"
#include "AutomaticPairing.h"
#include <iiSocietySync.h>
#include <SharedStorage.h>
#include <QDir>
#include <QLockFile>
#include <QSysInfo>
#include <QTimer>
#include <QScopedValueRollback>

namespace iiSocietyClient {
class Client::Private {
public:
    Client *q;
    QPointer<AccountSession> account;
    DiscoveryService *discovery;
    QHostAddress bindAddress;
    std::unique_ptr<NearbyDevices> nearby;
    std::unique_ptr<AutomaticPairing> pairing;
    iiServerHost::LanPeer lan;
    iiServerHost::Peer relay;
    iiSocietySync::Controller sync;
    QTimer timer;
    std::unique_ptr<QLockFile> lease;
    QString container, message;
    QJsonObject binding;
    QUrl server;
    QByteArray credential;
    bool active = false, updating = false, online = false;
    explicit Private(Client *owner, AccountSession *session, DiscoveryService *service, QHostAddress bind)
        : q(owner), account(session), discovery(service), bindAddress(bind), sync([this](auto peer, auto payload) {
            return server.isEmpty() ? lan.request(peer, payload) : relay.request(peer, payload);
        }) {
        timer.setInterval(1000);
        QObject::connect(&timer, &QTimer::timeout, q, [this] { update(); });
        QObject::connect(&sync, &iiSocietySync::Controller::progress, q, &Client::progress);
        QObject::connect(&sync, &iiSocietySync::Controller::synchronized, q, [this] { emit q->synchronized(); });
        QObject::connect(&sync, &iiSocietySync::Controller::changed, q, [this] {
            if (!sync.errorString().isEmpty()) message = sync.errorString();
            emit q->changed();
        });
        const auto response = [this](const QString &id, const QJsonObject &result, const QString &) { sync.receive(id, result); };
        QObject::connect(&lan, &iiServerHost::LanPeer::completed, q, response);
        QObject::connect(&relay, &iiServerHost::Peer::completed, q, response);
        QObject::connect(&lan, &iiServerHost::LanPeer::paired, q, [this](const QString &host) {
            if (nearby) nearby->complete(); update();
        });
        QObject::connect(&relay, &iiServerHost::Peer::peersChanged, q, [this] { update(); });
        QObject::connect(&relay, &iiServerHost::Peer::stateChanged, q, [this] { update(); });
    }
    void initialize() {
        if (nearby) return;
        if (!account) account = new AccountSession(q);
        nearby = std::make_unique<NearbyDevices>(discovery, bindAddress, q);
        pairing = std::make_unique<AutomaticPairing>(nearby.get(), &lan, [] { return false; }, [this](const QString &link) {
            return lan.join(link, nearby->deviceId(), nearby->deviceName());
        }, [this] { lan.stop(); }, q);
        QObject::connect(account, &AccountSession::changed, q, [this] { update(); });
        QObject::connect(account, &AccountSession::pairingCredentialsChanged, q, [this] { update(); });
        QObject::connect(account, &AccountSession::pairingStateRestored, q, [this] { update(); });
        QObject::connect(account, &AccountSession::pairingCredentialsRequired, account, &AccountSession::requestPairingCredentials);
        QObject::connect(account, &AccountSession::sessionEnding, q, [this] { transportStop(); });
        QObject::connect(nearby.get(), &NearbyDevices::identityEnding, &lan, &iiServerHost::LanPeer::cancelPairing);
        QObject::connect(nearby.get(), &NearbyDevices::changed, q, [this] { peers(); });
    }
    void transportStop() {
        online = false;
        sync.closeAndWait();
        if (nearby) nearby->clear();
        lan.stop(); relay.stop(); server = QUrl(); credential.clear();
        lease.reset();
    }
    void peers() {
        if (!active || !account || !account->signedIn() || !lease || !lease->isLocked()) return;
        const auto host = binding.value("host").toString(); QStringList authorized;
        if (server.isEmpty()) {
            if (nearby->authenticated() && lan.connected() && lan.pairedDeviceIds().contains(host)) authorized.append(host);
        } else if (relay.isReady() && relay.accountId() == account->manager()->account()->sub()) {
            for (const auto &value : relay.peers()) if (value.toObject().value("peerId") == host) authorized.append(host);
        }
        sync.setPeers(authorized, authorized);
        online = !authorized.isEmpty();
        message = authorized.isEmpty() ? "Waiting for the Society host." : "Connected to the Society host.";
        if (authorized.isEmpty() && server.isEmpty() && pairing) message = pairing->status();
        if (authorized.isEmpty() && !server.isEmpty() && !relay.errorString().isEmpty()) message = relay.errorString();
        if (authorized.isEmpty() && server.isEmpty() && !lan.errorString().isEmpty()) message = lan.errorString();
        emit q->changed();
    }
    void update() {
        if (!active || !account || updating) return;
        QScopedValueRollback guard(updating, true);
        if (!account->signedIn()) { transportStop(); message = "Sign in to Society on this device to connect to its host."; emit q->changed(); return; }
        const auto scope = account->storageScope();
        if (scope != binding.value("scope").toString()) { transportStop(); message = "The Society storage belongs to a different account."; emit q->changed(); return; }
        if (!lease) {
            lease = std::make_unique<QLockFile>(QDir(container).filePath(".society-sync/client-session.lock"));
            lease->setStaleLockTime(0);
        }
        if (!lease->isLocked() && !lease->tryLock()) { message = "Waiting for the other Society session to finish."; emit q->changed(); return; }
        sync.open(container, scope);
        const auto device = account->manager()->deviceInfo();
        const auto name = device.value("name").toString().isEmpty() ? QSysInfo::machineHostName() : device.value("name").toString();
        const QUrl configured(account->serverConfiguration().value("url").toString());
        if (configured.isEmpty()) {
            if (!server.isEmpty()) { relay.stop(); server = QUrl(); credential.clear(); }
            nearby->setIdentity(scope, device.value("id").toString(), name, device.value("type").toString(), false);
            nearby->setCredentials(account->pairingCredentials(), false, binding.value("host").toString());
            pairing->setEnabled(account->automaticPairingEnabled());
            account->requestPairingCredentials();
        } else {
            pairing->setEnabled(false); nearby->clear();
            const bool valid = configured.isValid() && !configured.host().isEmpty() && configured.userInfo().isEmpty()
                && !configured.hasQuery() && !configured.hasFragment()
                && (configured.scheme() == "wss" || (configured.scheme() == "ws" && QHostAddress(configured.host()).isLoopback()));
            const auto proof = account->relayCredential();
            if (!valid || proof.isEmpty()) { message = "The saved Society server session is unavailable."; emit q->changed(); return; }
            if (server != configured || credential != proof) {
                lan.stop(); relay.stop(); server = configured; credential = proof;
                iiServerHost::PeerOptions options;
                options.relayUrl = server; options.credential = proof; options.peerId = device.value("id").toString(); options.name = name.left(128);
                options.service = "com.iisacc.society.files"; options.hostFiles = false; options.localHostingEnabled = false;
                relay.start(options, [this](const QString &peer, const QJsonObject &payload) { return sync.handle(peer, payload); });
            }
        }
        peers();
    }
};
Client::Client(QObject *parent) : Client(nullptr, nullptr, parent) {}
Client::Client(AccountSession *account, DiscoveryService *discovery, QObject *parent)
    : Client(account, discovery, QHostAddress::AnyIPv4, parent) {}
Client::Client(AccountSession *account, DiscoveryService *discovery, QHostAddress bindAddress, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this, account, discovery, bindAddress)) {}
Client::~Client() { stop(); }
bool Client::start(const QString &container) {
    if (d->active && d->container == container) return true;
    stop();
    const auto storage = iiSocietyContainer::SharedStorage::open(container);
    if (!storage) { d->message = "Open the Society storage map on this device first."; emit changed(); return false; }
    d->container = storage->drive().rootPath(); d->binding = iiSocietySync::Replica::binding(d->container);
    if (d->binding.isEmpty()) { d->message = "Using the local Society host storage."; emit changed(); return true; }
    d->active = true; d->initialize(); d->timer.start(); d->update(); return true;
}
void Client::stop() { d->active = false; d->timer.stop(); d->transportStop(); }
void Client::synchronizeNow() { d->update(); d->sync.synchronizeNow(); }
QString Client::status() const { return d->message; }
bool Client::connected() const { return d->active && d->online; }
}
