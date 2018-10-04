#pragma once
#include "abstractnamespaceitem.h"

namespace ConnectionsTree {

class ServerItem;

class DatabaseItem : public AbstractNamespaceItem {
 public:
  DatabaseItem(unsigned int index, int keysCount,
               QSharedPointer<Operations> operations,
               QWeakPointer<TreeItem> parent, Model& model);

  ~DatabaseItem();

  QByteArray getName() const override;

  QByteArray getFullPath() const override;

  QString getDisplayName() const override;

  QString getType() const override { return "database"; }

  bool isEnabled() const override;

  void notifyModel() override;

  QVariantMap metadata() const override;

  void setMetadata(const QString&, QVariant) override;

 protected:
  void loadKeys(std::function<void()> callback = std::function<void()>());
  void unload(bool notify = true);
  void reload(std::function<void()> callback = std::function<void()>());
  void performLiveUpdate();
  void filterKeys(const QRegExp& filter);
  void resetFilter();

 private:
  QSharedPointer<QTimer> liveUpdateTimer();
  bool isLiveUpdateEnabled() const;

 private:
  unsigned int m_keysCount;
  QSharedPointer<QTimer> m_liveUpdateTimer;
};

}  // namespace ConnectionsTree
