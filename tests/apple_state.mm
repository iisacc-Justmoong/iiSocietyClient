#include <iiSocietyClient/GroupSessionStore.h>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <QTest>
#import <Security/Security.h>

iisacc::accounts::SessionStore *societyAppleStateKeyStore(QObject *parent);

class AppleStateTests : public QObject {
    Q_OBJECT
private slots:
    void adHocProcessesUseAnEncryptedDevelopmentLocation() {
        // This executable deliberately has the linker's ad-hoc signature and
        // no sandbox entitlement. It must never address a protected App Group.
        SecCodeRef code = nullptr;
        QCOMPARE(SecCodeCopySelf(kSecCSDefaultFlags, &code), errSecSuccess);
        CFDictionaryRef information = nullptr;
        const auto status = SecCodeCopySigningInformation(code, kSecCSSigningInformation, &information);
        CFRelease(code);
        QCOMPARE(status, errSecSuccess);
        const bool hasTeam = CFDictionaryContainsKey(information, kSecCodeInfoTeamIdentifier);
        CFRelease(information);
        QVERIFY(!hasTeam);
        const auto expected = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath("iisacc/Development Groups/group.com.iisacc.society/Library/Application Support/SocietyState");
        QCOMPARE(GroupSessionStore::defaultDirectory(), expected);
        QCoreApplication::setApplicationName("SocietyDaemon");
        QCOMPARE(GroupSessionStore::defaultDirectory(), expected);
        QCoreApplication::setApplicationName("Society");
        QCOMPARE(GroupSessionStore::defaultDirectory(), expected);
    }
    void nativeKeychainRoundTrip() {
        // Only a disposable, noncredential item is touched. The encrypted file
        // regression suite separately exercises restart and revocation races.
        std::unique_ptr<iisacc::accounts::SessionStore> keys(societyAppleStateKeyStore(nullptr));
        const auto key = "native-storage-probe-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QByteArray fixture("Synthetic Society storage verification");
        using Store = iisacc::accounts::SessionStore;
        Store::Result result; bool done = false;
        keys->write(key, fixture, [&](auto value) { result = value; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(result.error, Store::Error::None);
        done = false;
        keys->read(key, [&](auto value) { result = value; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        const bool matched = result.error == Store::Error::None && result.data == fixture;
        done = false;
        keys->remove(key, [&](auto value) { result = value; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(result.error, Store::Error::None);
        QVERIFY(matched);
        done = false;
        keys->read(key, [&](auto value) { result = value; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(result.error, Store::Error::Missing);
    }
};
QTEST_GUILESS_MAIN(AppleStateTests)
#include "apple_state.moc"
