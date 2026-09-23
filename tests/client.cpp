#include <iiSocietyClient/Client.h>
#include <iiSocietyClient/PairingCredentials.h>
#include <iiSocietyClient/AccountSession.h>
#include <iiSocietyClient/NearbyDevices.h>
#include <iiSocietyClient/AutomaticPairing.h>
#include <iiSocietySync.h>
#include <StorageMap.h>
#include "AccountServer.h"
#include "MemorySessionStore.h"
#include "FakeDiscoveryService.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
class ClientTests : public QObject {
    Q_OBJECT
private slots:
    void accountProofRejectsTamperingExpiryAndCrossAccount() {
        AccountServer authority; QVERIFY(authority.server.listen(QHostAddress::LocalHost));
        MemorySessionStore state; AccountSession account(authority.url(), &state, nullptr);
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn());
        QTRY_VERIFY(!account.busy()); account.requestPairingCredentials(); QTRY_VERIFY(!account.pairingCredentials().isEmpty());
        const auto credentials = account.pairingCredentials(); const QByteArray context("client\nunique-offer-and-nonce");
        const auto proof = SocietyPairingCredentials::sign(credentials, context); QVERIFY(!proof.isEmpty());
        QVERIFY(SocietyPairingCredentials::verify(credentials, context, proof));
        QVERIFY(!SocietyPairingCredentials::verify(credentials, "host\nunique-offer-and-nonce", proof));
        QVERIFY(!SocietyPairingCredentials::verify(credentials, context + "other-offer", proof));
        auto other = credentials; other.insert("scope", QString(64, 'f'));
        QVERIFY(!SocietyPairingCredentials::verify(other, context, proof));
        auto expired = credentials; expired.insert("expiresAt", QDateTime::currentDateTimeUtc().addSecs(-1).toString(Qt::ISODate));
        QVERIFY(SocietyPairingCredentials::sign(expired, context).isEmpty());
        QVERIFY(!SocietyPairingCredentials::verify(expired, context, proof));
        QVERIFY(!SocietyPairingCredentials::verify(credentials, context, {}));
    }
    void accountSessionAutomaticallyFetchesItsDriveLocation() {
        AccountServer authority; QVERIFY(authority.server.listen(QHostAddress::LocalHost));
        MemorySessionStore state; AccountSession account(authority.url(), &state, nullptr);
        QSignalSpy synced(account.manager(), &iisacc::accounts::AccountManager::containerDriveSynchronized);
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!"));
        QTRY_VERIFY(account.signedIn()); QTRY_VERIFY(!synced.isEmpty());
        QVERIFY(account.manager()->account()->societyContainerDrive().isEmpty());
        const QJsonObject location{{"containerId", "b413adf3-0642-4565-941f-aa1940d25c51"},
            {"imagePath", "/Volumes/Moved/Society.sparsebundle"}, {"hostDeviceId", QString(64, 'a')},
            {"revision", "1d02e288-9704-4c5a-979e-f9b60d1799ca"}};
        authority.profile.insert("societyContainerDrive", location);
        bool refreshed = false;
        QTRY_VERIFY(refreshed || (refreshed = account.manager()->refreshContainerDrive()));
        QTRY_COMPARE(account.manager()->account()->societyContainerDrive(), location.toVariantMap());
        QVERIFY(account.logout()); QTRY_VERIFY(!account.signedIn());
        QVERIFY(account.manager()->account()->societyContainerDrive().isEmpty());
    }

    void downloadsAndUploadsWithoutASocietyApplicationProcess_data() {
        QTest::addColumn<bool>("useRelay");
        QTest::newRow("saved-server") << true;
        QTest::newRow("automatic-local-network") << false;
    }
    void downloadsAndUploadsWithoutASocietyApplicationProcess() {
        QFETCH(bool, useRelay);
        AccountServer authority; QVERIFY(authority.server.listen(QHostAddress::LocalHost)); QVERIFY(!authority.profile.isEmpty());
        MemorySessionStore state; AccountSession account(authority.url(), &state, nullptr);
        auto device = account.manager()->deviceInfo(); device["id"] = QString(64, '2');
        QVERIFY(account.manager()->setDeviceInfo(device));
        QVERIFY(account.login("builder@example.com", "FixtureOnly1!")); QTRY_VERIFY(account.signedIn());
        QTRY_VERIFY(!account.busy());
        QTemporaryDir hostRoot(CLIENT_TEST_DIRECTORY "/host-XXXXXX"), phoneRoot(CLIENT_TEST_DIRECTORY "/phone-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(hostRoot.path())); QVERIFY(iiSocietyContainer::SocietyDrive::create(phoneRoot.path()));
        QFile model(hostRoot.filePath("Models/model.safetensors")); QVERIFY(model.open(QIODevice::WriteOnly)); model.write(QByteArray(800123, 'm')); model.close();
        const auto hostId = QString(64, '1'); const auto phoneId = device.value("id").toString(); const auto scope = authority.scope();
        authority.profile.insert("societyContainerDrive", QJsonObject{{"hostDeviceId", hostId},
            {"containerId", iiSocietyContainer::SocietyDrive::open(hostRoot.path())->identifier()},
            {"revision", "1d02e288-9704-4c5a-979e-f9b60d1799ca"}, {"imagePath", "/Volumes/Society.sparsebundle"}});
        bool refreshed = false;
        QTRY_VERIFY(refreshed || (refreshed = account.manager()->refreshContainerDrive()));
        QTRY_COMPARE(account.manager()->account()->societyContainerDrive().value("hostDeviceId").toString(), hostId);
        // Fixture for the one-time Society setup: bind to the authenticated
        // host and receive its storage map, without downloading its payload.
        {
            iiSocietySync::Replica host, phone;
            QVERIFY(host.open(hostRoot.path(), scope)); QVERIFY(phone.open(phoneRoot.path(), scope));
            iiSocietySync::Synchronizer initial(&phone); initial.setContentPolicy(iiSocietySync::Synchronizer::ContentPolicy::MetadataFirst);
            QSignalSpy done(&initial, &iiSocietySync::Synchronizer::finished);
            connect(&initial, &iiSocietySync::Synchronizer::requestReady, this, [&](auto id, auto, auto wire) {
                initial.receive(id, {{"ok", true}, {"result", host.handle(phoneId, wire.value("message").toObject())}});
            });
            QVERIFY(initial.start(hostId)); QTRY_COMPARE(done.size(), 1); QVERIFY(done[0][1].toBool());
        }
        iiServerHost::RelayServer relay([&](const auto &, auto done) { done({authority.profile.value("sub").toString(), QDateTime::currentDateTimeUtc().addSecs(300)}); });
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        const QUrl endpoint(QString("ws://127.0.0.1:%1").arg(relay.port()));
        if (useRelay) QTRY_VERIFY(account.setServerConfiguration({{"url", endpoint.toString()}, {"host", false}}));
        iiSocietySync::Controller hosting({}); hosting.open(hostRoot.path(), scope); QTRY_VERIFY(hosting.available());
        hosting.setPeers({phoneId}, {});
        iiServerHost::Peer host;
        const auto handle = [&](const QString &peer, const QJsonObject &payload) {
            if (payload.value("op") == "society.generation")
                return QJsonObject{{"ok", true}, {"peer", peer}, {"echo", payload}};
            return hosting.handle(peer, payload);
        };
        iiServerHost::PeerOptions options; options.relayUrl = endpoint; options.credential = "fixture";
        options.peerId = hostId; options.name = "Society host"; options.service = "com.iisacc.society.files"; options.localEnabled = false;
        if (useRelay) {
            QVERIFY(host.start(options, handle)); QTRY_VERIFY(host.isReady());
        }
        FakeDiscoveryService discovery, hostDiscovery;
        NearbyDevices hostNearby(&hostDiscovery, QHostAddress::LocalHost);
        iiServerHost::LanPeer hostLan;
        hostLan.setPairingAuthentication([&](const QByteArray &context) { return SocietyPairingCredentials::sign(account.pairingCredentials(), context); },
            [&](const QByteArray &context, const QJsonObject &proof) { return SocietyPairingCredentials::verify(account.pairingCredentials(), context, proof); });
        connect(&hostLan, &iiServerHost::LanPeer::completed, &hosting, [&](auto id, auto response, auto) { hosting.receive(id, response); });
        AutomaticPairing hostPairing(&hostNearby, &hostLan, [&] {
            return hostLan.hosting() || hostLan.startHost(hostId, "Society host", [&](auto peer, auto payload) {
                if (payload.value("op") == "list") return QJsonObject{{"ok", true}, {"entries", QJsonArray()}};
                return handle(peer, payload);
            }, {"127.0.0.1"}, QHostAddress::LocalHost);
        }, [](auto) { return false; }, [&] { hostLan.stop(); });
        QTimer announcements;
        if (!useRelay) {
            account.requestPairingCredentials(); QTRY_VERIFY(!account.pairingCredentials().isEmpty());
            hostNearby.setIdentity(scope, hostId, "Society host", "pc", true);
            hostNearby.setCredentials(account.pairingCredentials(), true, hostId);
            QVERIFY(hostNearby.authenticated());
            connect(&announcements, &QTimer::timeout, this, [&] {
                if (!discovery.record.isEmpty()) discovery.announceTo(hostDiscovery);
                if (!hostDiscovery.record.isEmpty()) hostDiscovery.announceTo(discovery);
            });
            announcements.start(50);
        }
        iiSocietyClient::Client consumer(&account, &discovery, QHostAddress::LocalHost);
        QVERIFY(consumer.requestHost({{"op", "society.generation"}}).isEmpty());
        QVERIFY(consumer.start(phoneRoot.path())); QTRY_VERIFY2_WITH_TIMEOUT(consumer.connected(),
            qPrintable(consumer.status() + ' ' + hostPairing.status() + ' ' + hostLan.phase() + ' ' + hostLan.errorString()), 10000);
        iiSocietyContainer::StorageMap map(*iiSocietyContainer::SocietyDrive::open(phoneRoot.path()));
        QSignalSpy responses(&consumer, &iiSocietyClient::Client::hostResponse);
        const auto rpc = consumer.requestHost({{"op", "society.generation"}, {"action", "submit"}});
        QVERIFY(!rpc.isEmpty()); QTRY_COMPARE(responses.size(), 1);
        QCOMPARE(responses[0][0].toString(), rpc);
        QCOMPARE(responses[0][1].toJsonObject().value("peer").toString(), phoneId);
        QVERIFY(!QFileInfo::exists(phoneRoot.filePath("Models/model.safetensors")));
        const auto request = map.request({"models/model.safetensors"}); QVERIFY(!request.isEmpty()); consumer.synchronizeNow();
        QTRY_COMPARE_WITH_TIMEOUT(map.requestState(request).value("state").toString(), QString("ready"), 15000);
        QVERIFY(map.available({"models/model.safetensors"}));
        QFile image(phoneRoot.filePath("Generation History/generated.png")); QVERIFY(image.open(QIODevice::WriteOnly)); image.write("completed output"); image.close();
        consumer.synchronizeNow();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(hostRoot.filePath("Generation History/generated.png")), 15000);
        QVERIFY(account.logout()); QTRY_VERIFY(!account.signedIn()); QTRY_VERIFY(!consumer.connected());
        QVERIFY(consumer.requestHost({{"op", "society.generation"}}).isEmpty());
        consumer.stop(); hosting.closeAndWait(); host.stop(); relay.stop();
    }
};
QTEST_GUILESS_MAIN(ClientTests)
#include "client.moc"
