#include "app.h"

#include <qredisclient/redisclient.h>
#include <QMessageBox>
#include <QNetworkProxyFactory>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QSysInfo>
#include <QUrl>
#include <QtQml>

#include "common/tabviewmodel.h"
#include "events.h"
#include "models/configmanager.h"
#include "models/connectionconf.h"
#include "models/connectionsmanager.h"
#include "models/key-models/keyfactory.h"
#include "modules/bulk-operations/bulkoperationsmanager.h"
#include "modules/common/sortfilterproxymodel.h"
#include "modules/console/autocompletemodel.h"
#include "modules/console/consolemodel.h"
#include "modules/server-stats/serverstatsmodel.h"
#include "modules/updater/updater.h"
#include "modules/value-editor/formattersmanager.h"
#include "modules/value-editor/tabsmodel.h"
#include "modules/value-editor/valueviewmodel.h"
#include "qmlutils.h"

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv),
      m_engine(this),
      m_qmlUtils(QSharedPointer<QmlUtils>(new QmlUtils())),
      m_events(QSharedPointer<Events>(new Events())),
      m_logger(nullptr) {
  // Init components required for models and qml
  initAppInfo();
  processCmdArgs();
  initAppFonts();
  initRedisClient();
  initUpdater();
  installTranslator();
}

void Application::initModels() {
  ConfigManager confManager(m_settingsDir);

  QString config = confManager.getApplicationConfigPath("connections.json");

  if (config.isNull()) {
    QMessageBox::critical(
        nullptr, QObject::tr("Settings directory is not writable"),
        QString(QObject::tr(
            "RDM can't save connections file to settings directory. "
            "Please change file permissions or restart RDM as "
            "administrator.")));

    throw std::runtime_error("invalid connections config");
  }

  QSharedPointer<KeyFactory> keyFactory(new KeyFactory());

  m_keyValues =
      QSharedPointer<ValueEditor::TabsModel>(new ValueEditor::TabsModel(
          keyFactory.staticCast<ValueEditor::AbstractKeyFactory>()));

  connect(m_events.data(), &Events::openValueTab, m_keyValues.data(),
          &ValueEditor::TabsModel::openTab);
  connect(m_events.data(), &Events::newKeyDialog, m_keyValues.data(),
          &ValueEditor::TabsModel::openNewKeyDialog);
  connect(m_events.data(), &Events::closeDbKeys, m_keyValues.data(),
          &ValueEditor::TabsModel::closeDbKeys);

  m_connections = QSharedPointer<ConnectionsManager>(
      new ConnectionsManager(config, m_events));

  m_bulkOperations = QSharedPointer<BulkOperations::Manager>(
      new BulkOperations::Manager(m_connections));

  connect(m_events.data(), &Events::requestBulkOperation,
          m_bulkOperations.data(),
          &BulkOperations::Manager::requestBulkOperation);

  m_consoleModel = QSharedPointer<TabViewModel>(
      new TabViewModel(getTabModelFactory<Console::Model>()));

  connect(m_events.data(), &Events::openConsole, m_consoleModel.data(),
          &TabViewModel::openTab);

  m_serverStatsModel = QSharedPointer<TabViewModel>(
      new TabViewModel(getTabModelFactory<ServerStats::Model>()));

  connect(m_events.data(), &Events::openServerStats, this,
          [this](QSharedPointer<RedisClient::Connection> c) {
            m_serverStatsModel->openTab(c);
          });

  m_formattersManager = QSharedPointer<ValueEditor::FormattersManager>(
      new ValueEditor::FormattersManager());
  m_formattersManager->loadFormatters();

  m_consoleAutocompleteModel = QSharedPointer<Console::AutocompleteModel>(
      new Console::AutocompleteModel());
}

void Application::initAppInfo() {
  setApplicationName("RedisDesktopManager");
  setApplicationVersion(QString(RDM_VERSION));
  setOrganizationDomain("redisdesktop.com");
  setOrganizationName("redisdesktop");
  setWindowIcon(QIcon(":/images/logo.png"));
}

void Application::initAppFonts() {
  QSettings settings;
#ifdef Q_OS_MAC
  QString defaultFontName("Helvetica Neue");
  int defaultFontSize = 12;
#else
  QString defaultFontName("Open Sans");
  int defaultFontSize = 11;
#endif

  QString appFont = settings.value("app/appFont", defaultFontName).toString();
  int appFontSize = settings.value("app/appFontSize", defaultFontSize).toInt();

#ifdef Q_OS_LINUX
  if (appFont == "Open Sans") {
    int result = QFontDatabase::addApplicationFont("://fonts/OpenSans.ttc");

    if (result == -1) {
      appFont = "Ubuntu";
    }
  }
#endif

  qDebug() << "App font:" << appFont << appFontSize;
  QFont defaultFont(appFont, appFontSize);
  QApplication::setFont(defaultFont);
}

void Application::initProxySettings() {
  QSettings settings;
  QNetworkProxyFactory::setUseSystemConfiguration(
      settings.value("app/useSystemProxy", false).toBool());
}

void Application::registerQmlTypes() {
  qmlRegisterType<SortFilterProxyModel>("rdm.models", 1, 0,
                                        "SortFilterProxyModel");
  qRegisterMetaType<ServerConfig>();
}

void Application::registerQmlRootObjects() {
  m_engine.rootContext()->setContextProperty("appEvents", m_events.data());
  m_engine.rootContext()->setContextProperty("qmlUtils", m_qmlUtils.data());
  m_engine.rootContext()->setContextProperty("connectionsManager",
                                             m_connections.data());
  m_engine.rootContext()->setContextProperty("valuesModel", m_keyValues.data());
  m_engine.rootContext()->setContextProperty("formattersManager",
                                             m_formattersManager.data());
  m_engine.rootContext()->setContextProperty("consoleModel",
                                             m_consoleModel.data());
  m_engine.rootContext()->setContextProperty("serverStatsModel",
                                             m_serverStatsModel.data());
  m_engine.rootContext()->setContextProperty("bulkOperations",
                                             m_bulkOperations.data());
  m_engine.rootContext()->setContextProperty("consoleAutocompleteModel",
                                             m_consoleAutocompleteModel.data());
}

void Application::initQml() {
  if (m_renderingBackend == "auto") {
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    // Use software renderer on Windows & Linux by default
    QQuickWindow::setSceneGraphBackend(QSGRendererInterface::Software);
#endif
  } else {
    QQuickWindow::setSceneGraphBackend(m_renderingBackend);
  }

  registerQmlTypes();
  registerQmlRootObjects();

  try {
    m_engine.load(QUrl(QStringLiteral("qrc:///app.qml")));
  } catch (...) {
    qDebug() << "Failed to load app window. Retrying with software renderer...";
    QQuickWindow::setSceneGraphBackend(QSGRendererInterface::Software);
    m_engine.load(QUrl(QStringLiteral("qrc:///app.qml")));
  }

  qDebug() << "Rendering backend:" << QQuickWindow::sceneGraphBackend();
}

void Application::initUpdater() {
  m_updater = QSharedPointer<Updater>(new Updater());
  connect(m_updater.data(), SIGNAL(updateUrlRetrived(QString &)), this,
          SLOT(OnNewUpdateAvailable(QString &)));
}

void Application::installTranslator() {
  QSettings settings;
  QString preferredLocale = settings.value("app/locale", "system").toString();

  QString locale;

  if (preferredLocale == "system") {
    settings.setValue("app/locale", "system");
    locale = QLocale::system().uiLanguages().first().replace("-", "_");

    qDebug() << QLocale::system().uiLanguages();

    if (locale.isEmpty() || locale == "C") locale = "en_US";

    qDebug() << "Detected locale:" << locale;
  } else {
    locale = preferredLocale;
  }

  QTranslator *translator = new QTranslator((QObject *)this);
  if (translator->load(QString(":/translations/rdm_") + locale)) {
    qDebug() << "Load translations file for locale:" << locale;
    QCoreApplication::installTranslator(translator);
  } else {
    delete translator;
  }
}

void Application::processCmdArgs() {
  QCommandLineParser parser;
  QCommandLineOption settingsDir("settings-dir",
                                 "(Optional) Directory where RDM looks/saves "
                                 ".rdm directory with connections.json file",
                                 "settingsDir", QDir::homePath());
  QCommandLineOption renderingBackend(
      "rendering-backend",
      "(Optional) QML rendering backend [software|opengl|d3d12|'']",
      "renderingBackend", "auto");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption(settingsDir);
  parser.addOption(renderingBackend);
  parser.process(*this);

  m_settingsDir = parser.value(settingsDir);
  m_renderingBackend = parser.value(renderingBackend);
}

void Application::OnNewUpdateAvailable(QString &url) {
  QMessageBox::information(
      nullptr, "New update available",
      QString(QObject::tr(
                  "Please download new version of Redis Desktop Manager: %1"))
          .arg(url));
}
