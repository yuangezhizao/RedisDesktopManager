#include "treeoperations.h"

#include <asyncfuture.h>
#include <qredisclient/redisclient.h>
#include <QRegExp>
#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QSet>
#include <QtConcurrent>
#include <algorithm>

#include "app/events.h"
#include "app/models/connectionconf.h"
#include "connections-tree/items/namespaceitem.h"
#include "connections-tree/keysrendering.h"

TreeOperations::TreeOperations(
    QSharedPointer<RedisClient::Connection> connection,
    QSharedPointer<Events> events)
    : m_connection(connection), m_events(events), m_dbCount(0) {}

bool TreeOperations::loadDatabases(
    std::function<void(RedisClient::DatabaseList)> callback) {
  bool connected = m_connection->isConnected();

  if (connected) {
    try {
      m_connection->refreshServerInfo();
    } catch (const RedisClient::Connection::Exception& e) {
      emit m_events->error(
          (QObject::tr("Connection error: ") + QString(e.what())));
      return false;
    }
  } else {
    try {
      connected = m_connection->connect(true);
    } catch (const RedisClient::Connection::Exception& e) {
      emit m_events->error(QObject::tr("Connection error: ") +
                           QString(e.what()));
      return false;
    }
  }

  if (!connected) {
    emit m_events->error(
        QObject::tr("Cannot connect to server '%1'. Check log for details.")
            .arg(m_connection->getConfig().name()));
    return false;
  }

  RedisClient::DatabaseList availableDatabeses =
      m_connection->getKeyspaceInfo();

  if (m_connection->mode() != RedisClient::Connection::Mode::Cluster) {
    // detect all databases
    RedisClient::Response scanningResp;
    int lastDbIndex =
        (availableDatabeses.size() == 0) ? 0 : availableDatabeses.lastKey() + 1;

    if (m_dbCount > 0) {
      for (int index = lastDbIndex; index < m_dbCount; index++) {
        availableDatabeses.insert(index, 0);
      }
    } else {
      uint dbScanLimit = static_cast<ServerConfig>(m_connection->getConfig())
                             .databaseScanLimit();

      for (int index = lastDbIndex; index < dbScanLimit; index++) {
        try {
          scanningResp =
              m_connection->commandSync("select", QString::number(index));
        } catch (const RedisClient::Connection::Exception& e) {
          throw ConnectionsTree::Operations::Exception(
              QObject::tr("Connection error: ") + QString(e.what()));
        }

        if (!scanningResp.isOkMessage()) {
          break;
        }

        availableDatabeses.insert(index, 0);
        ++lastDbIndex;
      }
      m_dbCount = lastDbIndex;
    }
  }

  callback(availableDatabeses);
  return true;
}

QFuture<bool> TreeOperations::getDatabases(
    std::function<void(RedisClient::DatabaseList)> callback) {
  QFuture<bool> result =
      QtConcurrent::run(this, &TreeOperations::loadDatabases, callback);

  AsyncFuture::observe(result).subscribe(
      []() {},
      [this]() {
        QtConcurrent::run([this]() {
          auto oldConnection = m_connection;
          m_connection = QSharedPointer<RedisClient::Connection>(
              new RedisClient::Connection(oldConnection->getConfig()));
          oldConnection->disconnect();
        });
      });

  return result;
}

void TreeOperations::loadNamespaceItems(
    QSharedPointer<ConnectionsTree::AbstractNamespaceItem> parent,
    const QString& filter, std::function<void(const QString& err)> callback,
    QSet<QByteArray> expandedNs) {
  QString keyPattern =
      filter.isEmpty()
          ? static_cast<ServerConfig>(m_connection->getConfig()).keysPattern()
          : filter;

  auto renderingCallback =
      [this, callback, filter, parent, expandedNs](
          const RedisClient::Connection::RawKeysList& keylist,
          const QString& err) {
        if (!err.isEmpty()) {
          return callback(err);
        }

        auto settings = ConnectionsTree::KeysTreeRenderer::RenderingSettigns{
            QRegExp(filter), getNamespaceSeparator(), parent->getDbIndex()};

        AsyncFuture::observe(
            QtConcurrent::run(&ConnectionsTree::KeysTreeRenderer::renderKeys,
                              sharedFromThis(), keylist, parent, settings,
                              expandedNs))
            .subscribe([callback]() { callback(QString()); });
      };

  auto thinRenderingCallback =
      [this, callback, parent, expandedNs](
          const RedisClient::Connection::NamespaceItems& items,
          const QString& err) {
        if (!err.isEmpty()) {
          return callback(err);
        }

        ConnectionsTree::KeysTreeRenderer::renderNamespaceItems(
            sharedFromThis(), items, parent, expandedNs);

        callback(QString());
      };

  try {
    if (m_connection->mode() == RedisClient::Connection::Mode::Cluster) {
      m_connection->getClusterKeys(renderingCallback, keyPattern);
    } else {
      if (static_cast<ServerConfig>(m_connection->getConfig())
              .luaKeysLoading()) {
        m_connection->getNamespaceItems(thinRenderingCallback,
                                        getNamespaceSeparator(), filter,
                                        parent->getDbIndex());
      } else {
        m_connection->getDatabaseKeys(renderingCallback, keyPattern,
                                      parent->getDbIndex());
      }
    }

  } catch (const RedisClient::Connection::Exception& error) {
    callback(QString(QObject::tr("Cannot load keys: %1")).arg(error.what()));
  }
}

void TreeOperations::disconnect() { m_connection->disconnect(); }

QString TreeOperations::getNamespaceSeparator() {
  return static_cast<ServerConfig>(m_connection->getConfig())
      .namespaceSeparator();
}

QString TreeOperations::defaultFilter() {
  return static_cast<ServerConfig>(m_connection->getConfig()).keysPattern();
}

void TreeOperations::openKeyTab(ConnectionsTree::KeyItem& key,
                                bool openInNewTab) {
  emit m_events->openValueTab(m_connection, key, openInNewTab);
}

void TreeOperations::openConsoleTab(int dbIndex) {
  emit m_events->openConsole(m_connection, dbIndex);
}

void TreeOperations::openNewKeyDialog(int dbIndex,
                                      std::function<void()> callback,
                                      QString keyPrefix) {
  emit m_events->newKeyDialog(m_connection, callback, dbIndex, keyPrefix);
}

void TreeOperations::openServerStats() {
  emit m_events->openServerStats(m_connection);
}

void TreeOperations::duplicateConnection() {
  emit m_events->createNewConnection(m_connection->getConfig());
}

void TreeOperations::notifyDbWasUnloaded(int dbIndex) {
  emit m_events->closeDbKeys(m_connection, dbIndex);
}

void TreeOperations::deleteDbKey(ConnectionsTree::KeyItem& key,
                                 std::function<void(const QString&)> callback) {
  RedisClient::Command::Callback cmdCallback = [this, &key, &callback](
                                                   const RedisClient::Response&,
                                                   const QString& error) {
    if (!error.isEmpty()) {
      callback(QString(QObject::tr("Cannot remove key: %1")).arg(error));
      return;
    }

    key.setRemoved();
    QRegExp filter(key.getFullPath(), Qt::CaseSensitive, QRegExp::Wildcard);
    emit m_events->closeDbKeys(m_connection, key.getDbIndex(), filter);
  };

  try {
    m_connection->command({"DEL", key.getFullPath()}, this, cmdCallback,
                          key.getDbIndex());
  } catch (const RedisClient::Connection::Exception& e) {
    throw ConnectionsTree::Operations::Exception(
        QObject::tr("Delete key error: ") + QString(e.what()));
  }
}

void TreeOperations::deleteDbNamespace(ConnectionsTree::NamespaceItem& ns) {
  QString pattern =
      QString("%1%2*")
          .arg(QString::fromUtf8(ns.getFullPath()))
          .arg(static_cast<ServerConfig>(m_connection->getConfig())
                   .namespaceSeparator());
  QRegExp filter(pattern, Qt::CaseSensitive, QRegExp::Wildcard);

  int dbIndex = ns.getDbIndex();

  emit m_events->requestBulkOperation(
      m_connection, dbIndex, BulkOperations::Manager::Operation::DELETE_KEYS,
      filter, [this, dbIndex, filter, &ns]() {
        ns.setRemoved();
        emit m_events->closeDbKeys(m_connection, dbIndex, filter);
      });
}

void TreeOperations::flushDb(int dbIndex,
                             std::function<void(const QString&)> callback) {
  try {
    m_connection->flushDbKeys(dbIndex, callback);
  } catch (const RedisClient::Connection::Exception& e) {
    throw ConnectionsTree::Operations::Exception(
        QObject::tr("FlushDB error: ") + QString(e.what()));
  }
}

QString TreeOperations::mode() {
  if (m_connection->mode() == RedisClient::Connection::Mode::Cluster) {
    return QString("cluster");
  } else if (m_connection->mode() == RedisClient::Connection::Mode::Sentinel) {
    return QString("sentinel");
  } else {
    return QString("standalone");
  }
}

bool TreeOperations::isConnected() const { return m_connection->isConnected(); }

void TreeOperations::setConnection(QSharedPointer<RedisClient::Connection> c) {
  m_connection = c;
}
