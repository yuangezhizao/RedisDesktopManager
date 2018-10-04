#pragma once
#include <qredisclient/connection.h>
#include <QObject>
#include <QRegExp>
#include <QSharedPointer>
#include <QString>
#include <functional>
#include "modules/bulk-operations/bulkoperationsmanager.h"

namespace ConnectionsTree {
class KeyItem;
}

class Events : public QObject {
  Q_OBJECT

 signals:

  // Tabs
  void openValueTab(QSharedPointer<RedisClient::Connection> connection,
                    ConnectionsTree::KeyItem& key, bool inNewTab);

  void openConsole(QSharedPointer<RedisClient::Connection> connection,
                   int dbIndex);

  void openServerStats(QSharedPointer<RedisClient::Connection> connection);

  void closeDbKeys(QSharedPointer<RedisClient::Connection> connection,
                   int dbIndex,
                   const QRegExp& filter = QRegExp("*", Qt::CaseSensitive,
                                                   QRegExp::Wildcard));

  // Dialogs
  void requestBulkOperation(QSharedPointer<RedisClient::Connection> connection,
                            int dbIndex, BulkOperations::Manager::Operation op,
                            QRegExp keyPattern, std::function<void()> callback);

  void newKeyDialog(QSharedPointer<RedisClient::Connection> connection,
                    std::function<void()> callback, int dbIndex,
                    QString keyPrefix);

  // Connections
  void createNewConnection(RedisClient::ConnectionConfig config);

  // Notifications
  void error(const QString& msg);

  void log(const QString& msg);
};
