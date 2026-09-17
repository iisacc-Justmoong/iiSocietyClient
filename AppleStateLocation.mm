#include <QString>
#include <QTimer>
#include <iiAcountManager/SessionStore.h>
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <TargetConditionals.h>

namespace {
NSString *groupIdentifier() {
    NSString *group = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"SocietyAppGroup"];
    return group.length ? group : @"group.com.iisacc.society";
}
// The wrapping key has no app-sandbox restore marker. iOS uses the App Group's
// Keychain access group, so reinstalling one participating app cannot orphan it.
class AppleGroupKeyStore final : public iisacc::accounts::SessionStore {
public:
    using SessionStore::SessionStore;
    NSMutableDictionary *query(const QString &key) {
        NSMutableDictionary *result = [@{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService:[groupIdentifier() stringByAppendingString:@".encrypted-state"],
            (__bridge id)kSecAttrAccount:key.toNSString()} mutableCopy];
#if TARGET_OS_IPHONE
        result[(__bridge id)kSecAttrAccessGroup] = groupIdentifier();
#endif
        return result;
    }
    void read(const QString &key, Completion done) override {
        QTimer::singleShot(0, this, [this, key, done] {
            @autoreleasepool {
                auto *q = query(key); q[(__bridge id)kSecReturnData] = @YES;
                q[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
#if TARGET_OS_IPHONE
                q[(__bridge id)kSecUseAuthenticationUI] = (__bridge id)kSecUseAuthenticationUIFail;
#endif
                CFTypeRef value = nullptr;
                const auto status = SecItemCopyMatching((__bridge CFDictionaryRef)q, &value);
                if (status != errSecSuccess && status != errSecItemNotFound) qWarning("Society group key read failed: %d", int(status));
                NSData *data = CFBridgingRelease(value);
                done({status == errSecSuccess ? Error::None : status == errSecItemNotFound ? Error::Missing : Error::Unavailable,
                    status == errSecSuccess ? QByteArray(static_cast<const char *>(data.bytes), data.length) : QByteArray()});
            }
        });
    }
    void write(const QString &key, const QByteArray &data, Completion done) override {
        QTimer::singleShot(0, this, [this, key, data, done] {
            @autoreleasepool {
                auto *q = query(key); q[(__bridge id)kSecValueData] = [NSData dataWithBytes:data.constData() length:data.size()];
#if TARGET_OS_IPHONE
                q[(__bridge id)kSecAttrAccessible] = (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
#endif
                const auto status = SecItemAdd((__bridge CFDictionaryRef)q, nullptr);
                if (status != errSecSuccess && status != errSecDuplicateItem) qWarning("Society group key write failed: %d", int(status));
                if (status == errSecDuplicateItem) {
                    read(key, [data, done](Result existing) { done({existing.error == Error::None && existing.data == data ? Error::None : Error::Unavailable, {}}); });
                } else done({status == errSecSuccess ? Error::None : Error::Unavailable, {}});
            }
        });
    }
    void remove(const QString &key, Completion done) override {
        QTimer::singleShot(0, this, [this, key, done] {
            const auto status = SecItemDelete((__bridge CFDictionaryRef)query(key));
            done({status == errSecSuccess || status == errSecItemNotFound ? Error::None : Error::Unavailable, {}});
        });
    }
};
}
iisacc::accounts::SessionStore *societyAppleStateKeyStore(QObject *parent) { return new AppleGroupKeyStore(parent); }

QString societyAppleStateDirectory() {
    @autoreleasepool {
        NSURL *root = [[NSFileManager defaultManager] containerURLForSecurityApplicationGroupIdentifier:groupIdentifier()];
        if (!root) return {};
        return QString::fromNSString([[[root URLByResolvingSymlinksInPath]
            URLByAppendingPathComponent:@"Library/Application Support/SocietyState" isDirectory:YES] path]);
    }
}
bool societyProtectStatePath(const QString &path) {
    @autoreleasepool {
        NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
        NSError *error = nil;
        if (![url setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&error]) return false;
#if TARGET_OS_IPHONE
        if (![[NSFileManager defaultManager] setAttributes:@{NSFileProtectionKey:NSFileProtectionCompleteUntilFirstUserAuthentication}
            ofItemAtPath:path.toNSString() error:&error]) return false;
#endif
        return true;
    }
}
