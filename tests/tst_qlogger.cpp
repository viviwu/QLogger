#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <thread>
#include <vector>

#include "qlogger/qlogger.h"

Q_LOGGING_CATEGORY(testcat, "testcat")

namespace {

LogConfig makeConfig(const QString& logDir) {
  LogConfig config;
  config.logDir = logDir;
  config.logFileName = "test";
  config.maxFileSize = 1024 * 1024;
  config.maxFileCount = 5;
  config.minLevel = LogLevel::Debug;
  config.enableConsole = false;
  config.enableTimestamp = true;
  config.enableThreadId = false;
  config.asyncMode = false;
  config.flushInterval = 50;
  return config;
}

QString activeLogName() {
  return QString("test_%1.log")
      .arg(QDate::currentDate().toString("yyyyMMdd"));
}

QStringList logFiles(const QString& dir) {
  QDir logDir(dir);
  return logDir.entryList(QStringList() << "test_*.log", QDir::Files,
                          QDir::Name);
}

QString readAllLogs(const QString& dir) {
  QDir logDir(dir);
  QString content;
  const QStringList files = logFiles(dir);
  for (const QString& file : files) {
    QFile f(logDir.filePath(file));
    if (f.open(QIODevice::ReadOnly)) {
      content += QString::fromUtf8(f.readAll());
      if (!content.endsWith('\n')) {
        content += '\n';
      }
    }
  }
  return content;
}

void silentHandler(QtMsgType, const QMessageLogContext&, const QString&) {}

}  // namespace

class TestQLogger : public QObject {
  Q_OBJECT

 private slots:
  void init();
  void cleanup();

  void notInitializedDoesNotWrite();
  void initializeCreatesFile();
  void repeatedInitializeIsIdempotent();
  void initializeFailsForInvalidDirectory();

  void levelFiltering_data();
  void levelFiltering();

  void formattingOptions();
  void timestampCanBeDisabled();
  void utf8RoundTrip();

  void syncWriteIsImmediatelyVisible();
  void appendsAcrossRestarts();

  void rotatesWhenFileTooLarge();
  void cleanupKeepsOnlyMaxRotatedFiles();
  void sameSecondRotationDoesNotOverwrite();

  void asyncTimerFlushes();
  void asyncQueueThresholdFlushes();
  void asyncManualFlush();
  void setConfigTogglesAsyncTimer();

  void setConfigSwitchesDirectory();

  void concurrentWritesAreComplete();

  void shutdownStopsWriting();
};

void TestQLogger::init() {
  QLogger::instance().shutdown();
  qInstallMessageHandler(nullptr);
}

void TestQLogger::cleanup() {
  QLogger::instance().shutdown();
  qInstallMessageHandler(nullptr);
}

void TestQLogger::notInitializedDoesNotWrite() {
  QLogger::instance().shutdown();
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  qInstallMessageHandler(silentHandler);
  qInfo() << "should-not-be-written";
  QTest::qWait(20);
  qInstallMessageHandler(nullptr);

  QVERIFY(logFiles(dir.path()).isEmpty());
}

void TestQLogger::initializeCreatesFile() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  QVERIFY(initLogger(makeConfig(dir.path())));
  QVERIFY(QLogger::instance().isInitialized());
  QVERIFY(QFile::exists(QDir(dir.path()).filePath(activeLogName())));
}

void TestQLogger::repeatedInitializeIsIdempotent() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const LogConfig config = makeConfig(dir.path());
  QVERIFY(initLogger(config));
  QVERIFY(initLogger(config));

  QCOMPARE(logFiles(dir.path()).size(), 1);
}

void TestQLogger::initializeFailsForInvalidDirectory() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const QString filePath = QDir(dir.path()).filePath("not_a_directory");
  QFile file(filePath);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("x");
  file.close();

  QVERIFY(!initLogger(makeConfig(filePath)));
  QVERIFY(!QLogger::instance().isInitialized());
}

void TestQLogger::levelFiltering_data() {
  QTest::addColumn<int>("minLevel");
  QTest::addColumn<QStringList>("expected");

  const QStringList all = {"MSG_DEBUG", "MSG_INFO", "MSG_WARNING",
                           "MSG_CRITICAL"};

  QTest::newRow("Debug") << static_cast<int>(LogLevel::Debug) << all;
  QTest::newRow("Info")
      << static_cast<int>(LogLevel::Info)
      << QStringList{"MSG_INFO", "MSG_WARNING", "MSG_CRITICAL"};
  QTest::newRow("Warning")
      << static_cast<int>(LogLevel::Warning)
      << QStringList{"MSG_WARNING", "MSG_CRITICAL"};
  QTest::newRow("Critical")
      << static_cast<int>(LogLevel::Critical)
      << QStringList{"MSG_CRITICAL"};
  QTest::newRow("Fatal")
      << static_cast<int>(LogLevel::Fatal) << QStringList{};
}

void TestQLogger::levelFiltering() {
  QFETCH(int, minLevel);
  QFETCH(QStringList, expected);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LogConfig config = makeConfig(dir.path());
  config.minLevel = static_cast<LogLevel>(minLevel);
  QVERIFY(initLogger(config));

  qDebug("MSG_DEBUG");
  qInfo("MSG_INFO");
  qWarning("MSG_WARNING");
  qCritical("MSG_CRITICAL");
  flushLogger();

  const QString content = readAllLogs(dir.path());
  const QStringList all = {"MSG_DEBUG", "MSG_INFO", "MSG_WARNING",
                           "MSG_CRITICAL"};
  for (const QString& token : all) {
    QCOMPARE(content.contains(token), expected.contains(token));
  }
}

void TestQLogger::formattingOptions() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LogConfig config = makeConfig(dir.path());
  config.enableTimestamp = true;
  config.enableThreadId = true;
  QVERIFY(initLogger(config));

  qCInfo(testcat) << "category-message";
  flushLogger();

  const QString content = readAllLogs(dir.path());
  const QRegularExpression timestamp(
      "\\[\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3}\\]");
  QVERIFY(timestamp.match(content).hasMatch());
  QVERIFY(content.contains("[Thread:"));
  QVERIFY(content.contains("[testcat]"));
}

void TestQLogger::timestampCanBeDisabled() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LogConfig config = makeConfig(dir.path());
  config.enableTimestamp = false;
  QVERIFY(initLogger(config));

  qInfo("no-timestamp");
  flushLogger();

  const QString content = readAllLogs(dir.path());
  const QRegularExpression timestamp(
      "\\[\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3}\\]");
  QVERIFY(!timestamp.match(content).hasMatch());
  QVERIFY(content.contains("no-timestamp"));
}

void TestQLogger::utf8RoundTrip() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  QVERIFY(initLogger(makeConfig(dir.path())));
  qInfo().noquote() << QStringLiteral("中文日志");
  flushLogger();

  QVERIFY(readAllLogs(dir.path()).contains(QStringLiteral("中文日志")));
}

void TestQLogger::syncWriteIsImmediatelyVisible() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  QVERIFY(initLogger(makeConfig(dir.path())));
  qInfo("sync-visible");

  QVERIFY(readAllLogs(dir.path()).contains("sync-visible"));
}

void TestQLogger::appendsAcrossRestarts() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  const LogConfig config = makeConfig(dir.path());

  QVERIFY(initLogger(config));
  qInfo("first-run");
  flushLogger();
  shutdownLogger();

  QVERIFY(initLogger(config));
  qInfo("second-run");
  flushLogger();
  shutdownLogger();

  const QString content = readAllLogs(dir.path());
  QVERIFY(content.contains("first-run"));
  QVERIFY(content.contains("second-run"));
}

void TestQLogger::rotatesWhenFileTooLarge() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LogConfig config = makeConfig(dir.path());
  config.maxFileSize = 200;
  config.maxFileCount = 50;
  QVERIFY(initLogger(config));

  for (int i = 0; i < 50; ++i) {
    qInfo().noquote() << QString("rotation-line-%1-%2")
                             .arg(i)
                             .arg(QString(40, QLatin1Char('x')));
  }
  flushLogger();

  QVERIFY(logFiles(dir.path()).size() > 1);
}

void TestQLogger::cleanupKeepsOnlyMaxRotatedFiles() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LogConfig config = makeConfig(dir.path());
  config.maxFileCount = 2;
  QVERIFY(initLogger(config));

  for (int i = 0; i < 20; ++i) {
    qInfo().noquote() << QString("rotate-%1").arg(i);
    flushLogger();
    QLogger::instance().rotate();
  }
  shutdownLogger();

  QStringList files = logFiles(dir.path());
  QVERIFY(files.removeAll(activeLogName()) == 1);
  QVERIFY(files.size() <= 2);
}

void TestQLogger::sameSecondRotationDoesNotOverwrite() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LogConfig config = makeConfig(dir.path());
  config.maxFileCount = 100;
  QVERIFY(initLogger(config));

  QStringList tokens;
  for (int i = 0; i < 5; ++i) {
    const QString token = QString("rotate-token-%1").arg(i);
    tokens << token;
    qInfo().noquote() << token;
    flushLogger();
    QLogger::instance().rotate();
  }
  flushLogger();
  shutdownLogger();

  const QString content = readAllLogs(dir.path());
  for (const QString& token : tokens) {
    QVERIFY2(content.contains(token), qPrintable(token));
  }
  QVERIFY(logFiles(dir.path()).size() >= tokens.size());
}

void TestQLogger::asyncTimerFlushes() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LogConfig config = makeConfig(dir.path());
  config.asyncMode = true;
  config.flushInterval = 50;
  QVERIFY(initLogger(config));

  qInfo("async-timer-message");
  QTRY_VERIFY_WITH_TIMEOUT(
      readAllLogs(dir.path()).contains("async-timer-message"), 2000);
}

void TestQLogger::asyncQueueThresholdFlushes() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LogConfig config = makeConfig(dir.path());
  config.asyncMode = true;
  config.flushInterval = 60000;
  QVERIFY(initLogger(config));

  for (int i = 0; i < 150; ++i) {
    qInfo().noquote() << QString("queue-message-%1").arg(i);
  }

  QVERIFY(readAllLogs(dir.path()).contains("queue-message-"));
}

void TestQLogger::asyncManualFlush() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LogConfig config = makeConfig(dir.path());
  config.asyncMode = true;
  config.flushInterval = 60000;
  QVERIFY(initLogger(config));

  qInfo("manual-flush-message");
  QVERIFY(!readAllLogs(dir.path()).contains("manual-flush-message"));

  flushLogger();
  QVERIFY(readAllLogs(dir.path()).contains("manual-flush-message"));
}

void TestQLogger::setConfigTogglesAsyncTimer() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LogConfig config = makeConfig(dir.path());
  config.asyncMode = false;
  config.flushInterval = 50;
  QVERIFY(initLogger(config));

  qInfo("sync-before");
  QVERIFY(readAllLogs(dir.path()).contains("sync-before"));

  config.asyncMode = true;
  QLogger::instance().setConfig(config);
  qInfo("async-after-toggle");
  QTRY_VERIFY_WITH_TIMEOUT(
      readAllLogs(dir.path()).contains("async-after-toggle"), 2000);

  config.asyncMode = false;
  QLogger::instance().setConfig(config);
  qInfo("sync-after-toggle");
  QVERIFY(readAllLogs(dir.path()).contains("sync-after-toggle"));
}

void TestQLogger::setConfigSwitchesDirectory() {
  QTemporaryDir first;
  QTemporaryDir second;
  QVERIFY(first.isValid());
  QVERIFY(second.isValid());

  LogConfig config = makeConfig(first.path());
  QVERIFY(initLogger(config));

  qInfo("in-first-dir");
  flushLogger();

  config.logDir = second.path();
  QLogger::instance().setConfig(config);
  qInfo("in-second-dir");
  flushLogger();

  QVERIFY(readAllLogs(first.path()).contains("in-first-dir"));
  QVERIFY(!readAllLogs(first.path()).contains("in-second-dir"));
  QVERIFY(readAllLogs(second.path()).contains("in-second-dir"));
}

void TestQLogger::concurrentWritesAreComplete() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  LogConfig config = makeConfig(dir.path());
  config.maxFileSize = 100 * 1024 * 1024;
  QVERIFY(initLogger(config));

  const int threadCount = 8;
  const int perThread = 200;
  std::vector<std::thread> threads;
  threads.reserve(threadCount);
  for (int t = 0; t < threadCount; ++t) {
    threads.emplace_back([t]() {
      for (int i = 0; i < perThread; ++i) {
        qInfo().noquote() << QString("T%1:%2").arg(t).arg(i);
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  flushLogger();

  const QString content = readAllLogs(dir.path());
  QCOMPARE(content.count(QRegularExpression("T\\d+:\\d+")),
           threadCount * perThread);
}

void TestQLogger::shutdownStopsWriting() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  QVERIFY(initLogger(makeConfig(dir.path())));
  qInfo("before-shutdown");
  flushLogger();
  QVERIFY(readAllLogs(dir.path()).contains("before-shutdown"));

  shutdownLogger();
  QVERIFY(!QLogger::instance().isInitialized());

  qInstallMessageHandler(silentHandler);
  qInfo("after-shutdown");
  QTest::qWait(20);
  qInstallMessageHandler(nullptr);

  QVERIFY(!readAllLogs(dir.path()).contains("after-shutdown"));
}

QTEST_GUILESS_MAIN(TestQLogger)

#include "tst_qlogger.moc"
