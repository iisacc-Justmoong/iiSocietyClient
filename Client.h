#pragma once
#include "iiSocietyClientExport.h"
#include <QObject>
#include <QHostAddress>
#include <memory>
class AccountSession;
class DiscoveryService;
namespace iiSocietyClient {
// In-process Society storage service for an app using a local Society replica.
// Consumers never implement their own host credentials or file transport.
class IISOCIETYCLIENT_EXPORT Client final : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);
    Client(AccountSession *account, DiscoveryService *discovery, QObject *parent = nullptr);
    Client(AccountSession *account, DiscoveryService *discovery, QHostAddress bindAddress, QObject *parent = nullptr);
    ~Client() override;
    bool start(const QString &container);
    void stop();
    void synchronizeNow();
    QString status() const;
    bool connected() const;
signals:
    void changed();
    void synchronized();
    void progress(QString path, qint64 completed, qint64 total);
private:
    class Private;
    std::unique_ptr<Private> d;
};
}
