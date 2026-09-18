#pragma once
#include "iiSocietyClientExport.h"
#include <iiAcountManager/SessionStore.h>
#include <QHash>
#include <QLockFile>
#include <QPointer>
#include <QQueue>

// Encrypted account and pairing records belong to the OS-managed Society group,
// independently of the user's selectable Files container.
class IISOCIETYCLIENT_EXPORT GroupSessionStore final : public iisacc::accounts::SessionStore {
public:
    static GroupSessionStore *create(QObject *parent = nullptr);
    static QString defaultDirectory();
    GroupSessionStore(QString directory, SessionStore *keys, SessionStore *legacy,
                      QObject *parent = nullptr);
    void read(const QString &key, Completion completion) override;
    void write(const QString &key, const QByteArray &data, Completion completion) override;
    void remove(const QString &key, Completion completion) override;
    QString directory() const { return m_directory; }
    QString errorString() const { return m_error; }
private:
    enum class Operation { Read, Write, Remove };
    struct Request {
        Operation operation;
        QString key;
        QByteArray data;
        Completion completion;
        quint64 revision;
        QByteArray generation;
        qint64 lockDeadline = 0;
    };
    void enqueue(Request request);
    void next();
    void withKey(Request request, bool create, std::function<void(Request, QByteArray)> continuation);
    void store(Request request, const QByteArray &key, bool migrating = false);
    void finish(Request request, Result result);
    bool prepare();
    bool fail(const QString &reason) { m_error = reason; return false; }
    QString path(const QString &key) const;
    QByteArray generation(const QString &key) const;
    bool save(const QString &key, const QByteArray &bytes, const QString &suffix = {});
    QString m_directory, m_managedRoot, m_error;
    QPointer<SessionStore> m_keys, m_legacy;
    std::unique_ptr<QLockFile> m_lock;
    QHash<QString, quint64> m_revisions;
    QHash<QString, QByteArray> m_observedGenerations;
    QQueue<Request> m_pending;
    bool m_running = false;
};
