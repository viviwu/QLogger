#include <QApplication>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QSettings>
#include <QDir>
#include <QDebug>

#include "logger_test_widget.h"
#include "qlogger/qlogger.h"

void dumpAppPaths()
{
    qInfo() << "=== App identity ===";
    qInfo() << "applicationName   :" << QCoreApplication::applicationName();
    qInfo() << "applicationVersion:" << QCoreApplication::applicationVersion();
    qInfo() << "organizationName  :" << QCoreApplication::organizationName();
    qInfo() << "organizationDomain:" << QCoreApplication::organizationDomain();

    struct Item { QStandardPaths::StandardLocation loc; const char *name; };
    const Item items[] = {
        { QStandardPaths::AppDataLocation,       "AppDataLocation"        },
        { QStandardPaths::AppLocalDataLocation,  "AppLocalDataLocation"   },
        { QStandardPaths::AppConfigLocation,     "AppConfigLocation"      },
        { QStandardPaths::ConfigLocation,        "ConfigLocation"         },
        { QStandardPaths::GenericConfigLocation, "GenericConfigLocation"  },
        { QStandardPaths::GenericDataLocation,   "GenericDataLocation"    },
        { QStandardPaths::CacheLocation,         "CacheLocation"          },
        { QStandardPaths::GenericCacheLocation,  "GenericCacheLocation"   },
        { QStandardPaths::TempLocation,          "TempLocation"           },
        { QStandardPaths::HomeLocation,          "HomeLocation"           },
    };

    qInfo() << "=== QStandardPaths::writableLocation ===";
    for (const auto &it : items) {
        const QString label = QString::fromLatin1(it.name) + ":";
        qInfo().noquote() << label.leftJustified(26)
                          << QStandardPaths::writableLocation(it.loc);
    }

    // 该位置的所有可能目录（macOS 上通常只有一个）
    qInfo().noquote() << "standardLocations(AppDataLocation)  :"
                      << QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    qInfo().noquote() << "standardLocations(AppConfigLocation):"
                      << QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation);

    // ---- QSettings 实际落盘文件 ----
    QSettings native;   // 默认 NativeFormat + UserScope，使用 QCoreApplication 里的 org/app
    qInfo().noquote() << "QSettings NativeFormat file :" << native.fileName();

    QSettings ini(QSettings::IniFormat, QSettings::UserScope, "CSCO", "logger_app");
    qInfo().noquote() << "QSettings IniFormat file    :" << ini.fileName();

    // ---- 建议的日志目录（自己创建）----
    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                           + "/logs";
    QDir().mkpath(logDir);
    qInfo().noquote() << "Suggested log dir           :" << logDir;
}

void setupLogger() {
  LogConfig config;
  config.logDir = QStandardPaths::writableLocation(
                      QStandardPaths::AppDataLocation) +
                  "/logs";
  config.logFileName = "logger_app";
  config.maxFileSize = 1024 * 1024;
  config.maxFileCount = 5;
  config.enableConsole = true;
  config.enableTimestamp = true;
  config.enableThreadId = true;
  config.asyncMode = true;
  config.flushInterval = 500;

  initLogger(config);
}

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  // 设置应用程序信息
  app.setApplicationName("logger_app");
  app.setApplicationVersion("1.2.3");
  app.setOrganizationName("CSCO");

    setupLogger();

    LOG_INFO("QLogger info....");
    dumpAppPaths();

  // 创建并显示测试窗口
  LoggerTestWidget window;
  window.show();

  // 运行应用程序
  int result = app.exec();

  return result;
}
