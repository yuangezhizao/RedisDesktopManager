#pragma once
#include <QEnableSharedFromThis>
#include <QObject>
#include <QSharedPointer>
#include <functional>
#include "modules/bulk-operations/bulkoperationsmanager.h"
#include "modules/connections-tree/items/keyitem.h"
#include "modules/connections-tree/operations.h"

class Events;

class TreeOperations : public QObject,
                       public ConnectionsTree::Operations,
                       public QEnableSharedFromThis<TreeOperations> {
  Q_OBJECT
 public:
  TreeOperations(QSharedPointer<RedisClient::Connection> connection,
                 QSharedPointer<Events> events);

  QFuture<bool> getDatabases(
      std::function<void(RedisClient::DatabaseList)>) override;

  void loadNamespaceItems(
      QSharedPointer<ConnectionsTree::AbstractNamespaceItem> parent,
      const QString& filter, std::function<void(const QString& err)> callback,
      QSet<QByteArray> expandedNs) override;

  void disconnect() override;

  QString getNamespaceSeparator() override;

  QString defaultFilter() override;

  void openKeyTab(ConnectionsTree::KeyItem& key,
                  bool openInNewTab = false) override;

  void openConsoleTab(int dbIndex = 0) override;

  void openNewKeyDialog(int dbIndex, std::function<void()> callback,
                        QString keyPrefix = QString()) override;

  void openServerStats() override;

  void duplicateConnection() override;

  void notifyDbWasUnloaded(int dbIndex) override;

  void deleteDbKey(ConnectionsTree::KeyItem& key,
                   std::function<void(const QString&)> callback) override;

  void deleteDbNamespace(ConnectionsTree::NamespaceItem& ns) override;

  virtual void flushDb(int dbIndex,
                       std::function<void(const QString&)> callback) override;

  virtual QString mode() override;

  virtual bool isConnected() const override;

  void setConnection(QSharedPointer<RedisClient::Connection> c);

 protected:
  bool loadDatabases(std::function<void(RedisClient::DatabaseList)> callback);

 private:
  QSharedPointer<RedisClient::Connection> m_connection;
  QSharedPointer<Events> m_events;
  uint m_dbCount;
};
