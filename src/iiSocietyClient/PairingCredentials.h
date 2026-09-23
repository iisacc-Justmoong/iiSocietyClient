#pragma once
#include "iiSocietyClientExport.h"
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>

// The account service provisions bounded daily seeds. Discovery derives its
// five-minute authentication keys locally; no login token is used on the LAN.
namespace SocietyPairingCredentials {
IISOCIETYCLIENT_EXPORT bool valid(const QJsonObject &credentials, QDateTime now = QDateTime::currentDateTimeUtc());
IISOCIETYCLIENT_EXPORT QByteArray key(const QJsonObject &credentials, qint64 epoch, QDateTime now = QDateTime::currentDateTimeUtc());
IISOCIETYCLIENT_EXPORT QJsonObject sign(const QJsonObject &credentials, const QByteArray &context);
IISOCIETYCLIENT_EXPORT bool verify(const QJsonObject &credentials, const QByteArray &context, const QJsonObject &proof);
}
