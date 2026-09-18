#include "GroupSessionStore.h"
#include "StateCrypto.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <cstring>
#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#endif
#ifdef Q_OS_DARWIN
QString societyAppleStateDirectory();
bool societyProtectStatePath(const QString &path);
iisacc::accounts::SessionStore *societyAppleStateKeyStore(QObject *parent);
#endif

namespace {
constexpr auto tombstone = "SGS0";
constexpr auto version = "SGS1";
const QString masterKey = QStringLiteral("group-state-aes256-v1");
QByteArray context(const QString &key) { return "com.iisacc.society.group-state.v1:" + key.toUtf8(); }
}

QString GroupSessionStore::defaultDirectory() {
#ifdef Q_OS_DARWIN
    return societyAppleStateDirectory();
#elif defined(Q_OS_ANDROID)
    const QJniObject app = QNativeInterface::QAndroidApplication::context();
    const auto base = app.callObjectMethod("getNoBackupFilesDir", "()Ljava/io/File;");
    if (!base.isValid()) return {};
    return QDir(base.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString())
        .filePath("group.com.iisacc.society/State");
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath("iisacc/Group Containers/group.com.iisacc.society/State");
#endif
}
GroupSessionStore *GroupSessionStore::create(QObject *parent) {
    auto *result = new GroupSessionStore(defaultDirectory(), nullptr, nullptr, parent);
#ifdef Q_OS_DARWIN
    if (!result->m_directory.isEmpty()) result->m_managedRoot = QDir::cleanPath(result->m_directory + "/../../..");
    result->m_keys = societyAppleStateKeyStore(result);
#else
#ifdef Q_OS_ANDROID
    if (!result->m_directory.isEmpty()) result->m_managedRoot = QDir::cleanPath(result->m_directory + "/../..");
#endif
    result->m_keys = SessionStore::create("com.iisacc.society.group-state", result);
#endif
    result->m_legacy = SessionStore::create("com.iisacc.society", result);
    return result;
}
GroupSessionStore::GroupSessionStore(QString directory, SessionStore *keys, SessionStore *legacy, QObject *parent)
    : SessionStore(parent), m_directory(std::move(directory)), m_keys(keys), m_legacy(legacy) {}
bool GroupSessionStore::prepare() {
    if (m_directory.isEmpty() || !QDir::isAbsolutePath(m_directory) || !m_keys) return fail("The Society group location or key store is unavailable.");
    QString current = QDir::rootPath();
    if (!m_managedRoot.isEmpty()) {
        // Mobile sandboxes cannot inspect arbitrary ancestors such as /var or
        // /data/user. Start at the root returned by the OS entitlement API.
        QFileInfo root(m_managedRoot);
        if (root.isSymLink() || root.isJunction()) return fail("The managed Society group is redirected.");
        if (!root.exists() && !QDir().mkpath(m_managedRoot)) return fail("The managed Society group cannot be created.");
        root.refresh(); if (!root.isDir()) return fail("The managed Society group is unavailable.");
        current = m_managedRoot;
    }
    for (const auto &part : QDir(current).relativeFilePath(QDir::cleanPath(m_directory)).split('/')) {
        if (part.isEmpty() || part == "." || part == "..") return fail("The Society state path is invalid.");
        current = QDir(current).filePath(part);
        QFileInfo info(current);
        if (info.isSymLink() || info.isJunction()) return fail("The Society state directory is redirected.");
        if (!info.exists() && !QDir().mkdir(current)) return fail("The Society state directory cannot be created.");
        info.refresh(); if (!info.isDir()) return fail("The Society state directory is unavailable.");
    }
    if (!QFile::setPermissions(m_directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) return fail("Society state permissions could not be applied.");
#ifdef Q_OS_DARWIN
    if (!societyProtectStatePath(m_directory)) return fail("Society state data protection could not be applied.");
#endif
    return true;
}
QString GroupSessionStore::path(const QString &key) const {
    return QDir(m_directory).filePath(QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".state");
}
QByteArray GroupSessionStore::generation(const QString &key) const {
    if (!QDir::isAbsolutePath(m_directory)) return {};
    QFile file(path(key) + ".revocation"); const QFileInfo info(file);
    if (info.isSymLink() || info.isJunction()) return {};
    if (!info.exists()) return QByteArray(16, '\0');
    if (!info.isFile() || !file.open(QIODevice::ReadOnly) || file.size() != 16) return {};
    return file.readAll();
}
bool GroupSessionStore::save(const QString &key, const QByteArray &bytes, const QString &suffix) {
    const QFileInfo info(path(key) + suffix);
    if (info.isSymLink() || info.isJunction()) return fail("The Society state record is redirected.");
    QSaveFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) return fail("The Society state record could not be committed.");
#ifdef Q_OS_DARWIN
    return societyProtectStatePath(info.absoluteFilePath()) || fail("Society state record protection could not be applied.");
#else
    return true;
#endif
}
void GroupSessionStore::read(const QString &key, Completion completion) {
    const auto token = generation(key); m_observedGenerations[key] = token;
    enqueue({Operation::Read, key, {}, std::move(completion), m_revisions.value(key), token});
}
void GroupSessionStore::write(const QString &key, const QByteArray &data, Completion completion) {
    if (!m_observedGenerations.contains(key)) m_observedGenerations[key] = generation(key);
    enqueue({Operation::Write, key, data, std::move(completion), ++m_revisions[key], m_observedGenerations.value(key)});
}
void GroupSessionStore::remove(const QString &key, Completion completion) {
    const auto revision = ++m_revisions[key];
    // Commit revocation before asynchronous Keychain work or process exit.
    // The generation marker also invalidates an older write in another process.
    // A racing ciphertext write cannot make that generation readable again.
    const auto token = QUuid::createUuid().toRfc4122();
    bool ok = prepare() && save(key, token, ".revocation");
    if (ok) m_observedGenerations[key] = token;
    if (ok) save(key, tombstone);
    if (!ok) { completion({Error::Unavailable, {}}); return; }
    enqueue({Operation::Remove, key, {}, std::move(completion), revision, token});
}
void GroupSessionStore::enqueue(Request request) {
    m_pending.enqueue(std::move(request));
    if (!m_running) { m_running = true; QTimer::singleShot(0, this, [this] { next(); }); }
}
void GroupSessionStore::next() {
    if (m_pending.isEmpty()) { m_running = false; return; }
    auto request = m_pending.dequeue();
    m_error.clear();
    if (request.revision != m_revisions.value(request.key) || request.generation != generation(request.key)) { finish(std::move(request), {Error::Missing, {}}); return; }
    if (request.key.isEmpty() || request.key.size() > 512 || request.generation.size() != 16 || request.data.size() > MaximumSocietyStateBytes || !prepare()) {
        finish(std::move(request), {Error::Unavailable, {}}); return;
    }
    m_lock = std::make_unique<QLockFile>(QDir(m_directory).filePath("state.lock"));
    if (!request.lockDeadline) request.lockDeadline = QDateTime::currentMSecsSinceEpoch() + 10000;
    if (!m_lock->tryLock(0)) {
        m_lock.reset();
        if (QDateTime::currentMSecsSinceEpoch() < request.lockDeadline) {
            m_pending.prepend(std::move(request)); QTimer::singleShot(50, this, [this] { next(); }); return;
        }
        qWarning("Society group state remained busy in another store."); finish(std::move(request), {Error::Unavailable, {}}); return;
    }
    if (request.operation == Operation::Remove) {
        if (!m_legacy) { finish(std::move(request), {}); return; }
        const QPointer<GroupSessionStore> guard(this);
        m_legacy->remove(request.key, [guard, request](Result) mutable { if (guard) guard->finish(std::move(request), {}); });
        return;
    }
    if (request.operation == Operation::Write) {
        withKey(std::move(request), true, [this](Request r, QByteArray key) { store(std::move(r), key); }); return;
    }
    QFile file(path(request.key)); const QFileInfo info(file);
    if (info.isSymLink() || info.isJunction() || (info.exists() && (!info.isFile() || !file.open(QIODevice::ReadOnly) || file.size() > MaximumSocietyStateBytes + 48))) {
        finish(std::move(request), {Error::Unavailable, {}}); return;
    }
    if (!info.exists()) {
        if (request.generation != QByteArray(16, '\0')) { finish(std::move(request), {Error::Missing, {}}); return; }
        if (!m_legacy) { finish(std::move(request), {Error::Missing, {}}); return; }
        const QPointer<GroupSessionStore> guard(this);
        m_legacy->read(request.key, [guard, request](Result result) mutable {
            if (!guard) return;
            if (result.error != Error::None) { guard->finish(std::move(request), result); return; }
            request.data = result.data;
            guard->withKey(std::move(request), true, [guard](Request r, QByteArray key) {
                if (guard) guard->store(std::move(r), key, true);
            });
        }); return;
    }
    const auto bytes = file.readAll();
    if (bytes == tombstone) { finish(std::move(request), {Error::Missing, {}}); return; }
    if (!bytes.startsWith(version)) { finish(std::move(request), {Error::Unavailable, {}}); return; }
    if (bytes.mid(4, 16) != request.generation) { finish(std::move(request), {Error::Missing, {}}); return; }
    withKey(std::move(request), false, [this, bytes](Request r, QByteArray key) {
        const auto plain = openSocietyState(bytes.mid(20), key, context(r.key) + r.generation);
        finish(std::move(r), {plain.isEmpty() ? Error::Unavailable : Error::None, plain});
    });
}
void GroupSessionStore::withKey(Request request, bool create, std::function<void(Request, QByteArray)> continuation) {
    const QPointer<GroupSessionStore> guard(this);
    m_keys->read(masterKey, [guard, request, create, continuation](Result result) mutable {
        if (!guard) return;
        if (result.error == Error::None && result.data.size() == 32) { continuation(std::move(request), result.data); return; }
        if (result.error != Error::Missing || !create) { guard->fail("The Society group encryption key is unavailable."); guard->finish(std::move(request), {Error::Unavailable, {}}); return; }
        QByteArray key(32, Qt::Uninitialized);
        for (int index = 0; index < 32; index += 4) {
            const auto random = QRandomGenerator::system()->generate(); memcpy(key.data() + index, &random, 4);
        }
        guard->m_keys->write(masterKey, key, [guard, request, key, continuation](Result saved) mutable {
            if (!guard) return;
            if (saved.error != Error::None) { guard->fail("The Society group encryption key could not be saved."); guard->finish(std::move(request), {Error::Unavailable, {}}); return; }
            continuation(std::move(request), key);
        });
    });
}
void GroupSessionStore::store(Request request, const QByteArray &key, bool migrating) {
    if (request.revision != m_revisions.value(request.key) || request.generation != generation(request.key)) { finish(std::move(request), {Error::Missing, {}}); return; }
    const auto sealed = sealSocietyState(request.data, key, context(request.key) + request.generation);
    if (sealed.isEmpty() || !save(request.key, QByteArray(version) + request.generation + sealed)) { finish(std::move(request), {Error::Unavailable, {}}); return; }
    if (migrating && m_legacy) {
        const QPointer<GroupSessionStore> guard(this);
        m_legacy->remove(request.key, [guard, request](Result) mutable {
            if (guard) { const auto data = request.data; guard->finish(std::move(request), {Error::None, data}); }
        });
    } else finish(std::move(request), {});
}
void GroupSessionStore::finish(Request request, Result result) {
    m_lock.reset();
    if (request.revision != m_revisions.value(request.key) || request.generation != generation(request.key)) result = {Error::Missing, {}};
    const QPointer<GroupSessionStore> guard(this);
    request.completion(std::move(result));
    if (guard) QTimer::singleShot(0, this, [this] { next(); });
}
