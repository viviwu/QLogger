#include "qlogger.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QTextStream>
#include <memory>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

namespace {

// 防止消息处理器自身产生的日志造成递归
struct RecursionGuard {
  bool& flag;
  explicit RecursionGuard(bool& value) : flag(value) { flag = true; }
  ~RecursionGuard() { flag = false; }
};

}  // namespace

// 初始化日志系统
bool QLogger::initialize(const LogConfig& config) {
  {
    QMutexLocker locker(&m_mutex);

    // 防止重复初始化
    if (m_initialized.load(std::memory_order_acquire)) {
      return true;
    }

    m_config = config;

    if (!setupLogDirectory()) {
      return false;
    }

    if (!openLogFile()) {
      return false;
    }

    m_initialized.store(true, std::memory_order_release);
    setupAsyncTimer(m_config);
  }

  // 安装消息处理器（此时已经可以安全处理日志）
  qInstallMessageHandler(messageHandler);

  // 使用日志系统输出初始化成功消息
  LOG_INFO("QLogger initialized successfully");

  return true;
}

// 设置日志配置
void QLogger::setConfig(const LogConfig& config) {
  QMutexLocker locker(&m_mutex);

  if (!m_initialized.load(std::memory_order_acquire)) {
    m_config = config;
    return;
  }

  // 先把队列中的日志写入旧文件，再切换到新配置
  flushPendingLogs();
  closeLogFile();

  m_config = config;

  if (setupLogDirectory()) {
    openLogFile();
  }

  setupAsyncTimer(m_config);
}

// 获取当前配置
LogConfig QLogger::getConfig() const {
  QMutexLocker locker(&m_mutex);
  return m_config;
}

bool QLogger::isInitialized() const {
  return m_initialized.load(std::memory_order_acquire);
}

// 立即刷新日志到文件
void QLogger::flush() {
  QMutexLocker locker(&m_mutex);
  flushPendingLogs();
}

// 强制日志轮转
void QLogger::rotate() {
  QMutexLocker locker(&m_mutex);
  if (m_initialized.load(std::memory_order_acquire)) {
    rotateLogFile();
  }
}

// 清理旧日志文件
void QLogger::cleanup() {
  QMutexLocker locker(&m_mutex);
  if (m_initialized.load(std::memory_order_acquire)) {
    cleanupOldFiles();
  }
}

// 关闭日志系统
void QLogger::shutdown() {
  if (!m_initialized.load(std::memory_order_acquire)) {
    return;
  }

  // 先恢复默认日志处理器，避免在清理过程中继续处理日志
  qInstallMessageHandler(nullptr);

  QMutexLocker locker(&m_mutex);

  if (!m_initialized.load(std::memory_order_acquire)) {
    return;
  }

  if (m_flushTimer) {
    m_flushTimer->stop();
    delete m_flushTimer;
    m_flushTimer = nullptr;
  }

  flushPendingLogs();
  closeLogFile();
  m_initialized.store(false, std::memory_order_release);

  if (m_config.enableConsole) {
    fputs("[INFO] QLogger shutdown completed\n", stdout);
    fflush(stdout);
  }
}

// private slots:
void QLogger::onFlushTimer() {
  if (!m_initialized.load(std::memory_order_acquire)) {
    return;
  }

  QMutexLocker locker(&m_mutex);
  flushPendingLogs();
}

// private:
QLogger::QLogger() : m_initialized(false), m_flushTimer(nullptr) {
  // 确保在应用程序退出时自动清理
  if (qApp) {
    connect(qApp, &QCoreApplication::aboutToQuit, this, &QLogger::shutdown);
  }
}

QLogger::~QLogger() { shutdown(); }

void QLogger::handleMessage(QtMsgType type, const QMessageLogContext& context,
                            const QString& msg) {
  static thread_local bool inHandler = false;
  if (inHandler) {
    return;
  }
  RecursionGuard guard(inHandler);

  if (!m_initialized.load(std::memory_order_acquire)) {
    return;
  }

  // 取一份配置快照，避免在无锁状态下读取 m_config
  const LogConfig config = configSnapshot();

  if (!shouldLog(config, type)) {
    return;
  }

  LogEntry entry;
  entry.type = type;
  entry.message = msg;
  entry.category = context.category ? QString(context.category) : "default";
  entry.file = context.file ? QString(context.file) : "";
  entry.function = context.function ? QString(context.function) : "";
  entry.line = context.line;
  entry.timestamp = QDateTime::currentDateTime();
  entry.threadId = QThread::currentThreadId();

  // 输出到控制台（不持有文件锁）
  if (config.enableConsole) {
    outputToConsole(entry, config);
  }

  QMutexLocker locker(&m_mutex);

  if (!m_initialized.load(std::memory_order_acquire)) {
    return;
  }

  if (config.asyncMode) {
    // 异步模式：添加到队列
    m_logQueue.enqueue(entry);

    // 如果队列过长，立即刷新
    if (m_logQueue.size() > 100) {
      flushPendingLogs();
    }
  } else {
    // 同步模式：直接写入文件
    writeLogEntry(entry);
  }
}

LogLevel QLogger::toLogLevel(QtMsgType type) {
  // 注意：QtMsgType 的顺序与 LogLevel 不同（QtInfoMsg 在最后）
  switch (type) {
    case QtDebugMsg:
      return LogLevel::Debug;
    case QtInfoMsg:
      return LogLevel::Info;
    case QtWarningMsg:
      return LogLevel::Warning;
    case QtCriticalMsg:
      return LogLevel::Critical;
    case QtFatalMsg:
      return LogLevel::Fatal;
  }
  return LogLevel::Debug;
}

bool QLogger::shouldLog(const LogConfig& config, QtMsgType type) const {
  return toLogLevel(type) >= config.minLevel;
}

void QLogger::outputToConsole(const LogEntry& entry,
                              const LogConfig& config) {
  const QByteArray line = formatLogMessage(entry, config).toLocal8Bit();

  // 直接使用C风格输出，避免Qt的递归问题
  const bool isError =
      (entry.type == QtCriticalMsg || entry.type == QtFatalMsg);
  FILE* out = isError ? stderr : stdout;

  QMutexLocker locker(&m_consoleMutex);
  fprintf(out, "%s\n", line.constData());
  fflush(out);
}

QString QLogger::formatLogMessage(const LogEntry& entry,
                                  const LogConfig& config) const {
  QString result;
  QTextStream stream(&result);

  // 时间戳
  if (config.enableTimestamp) {
    stream << "[" << entry.timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz")
           << "] ";
  }

  // 日志级别
  stream << "[" << levelToString(entry.type) << "] ";

  // 线程ID
  if (config.enableThreadId) {
    stream << "[Thread:" << reinterpret_cast<quintptr>(entry.threadId) << "] ";
  }

  // 分类
  if (!entry.category.isEmpty() && entry.category != "default") {
    stream << "[" << entry.category << "] ";
  }

  // 消息内容
  stream << entry.message;

  // 文件和行号信息(仅Debug模式)
  if (entry.type == QtDebugMsg && !entry.file.isEmpty()) {
    QFileInfo fileInfo(entry.file);
    stream << " (" << fileInfo.fileName() << ":" << entry.line << ")";
  }

  return result;
}

QString QLogger::levelToString(QtMsgType type) const {
  switch (type) {
    case QtDebugMsg:
      return "DEBUG";
    case QtInfoMsg:
      return "INFO";
    case QtWarningMsg:
      return "WARN";
    case QtCriticalMsg:
      return "ERROR";
    case QtFatalMsg:
      return "FATAL";
    default:
      return "UNKNOWN";
  }
}

LogConfig QLogger::configSnapshot() const {
  QMutexLocker locker(&m_mutex);
  return m_config;
}

QString QLogger::logDirAbsolute() const {
  return QDir(m_config.logDir).absolutePath();
}

bool QLogger::setupLogDirectory() {
  const QString fullPath = logDirAbsolute();

  if (!QDir(fullPath).exists()) {
    if (!QDir().mkpath(fullPath)) {
      if (m_config.enableConsole) {
        fprintf(stderr, "[ERROR] Failed to create log directory: %s\n",
                fullPath.toLocal8Bit().constData());
      }
      return false;
    }
  }
  return true;
}

bool QLogger::openLogFile() {
  QDir dir(logDirAbsolute());
  const QString fileName = dir.absoluteFilePath(
      QString("%1_%2.log")
          .arg(m_config.logFileName)
          .arg(QDateTime::currentDateTime().toString("yyyyMMdd")));

  m_currentLogFile = std::make_unique<QFile>(fileName);

  if (m_currentLogFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
    m_logStream = std::make_unique<QTextStream>(m_currentLogFile.get());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_logStream->setEncoding(QStringConverter::Utf8);
#else
    m_logStream->setCodec("UTF-8");
#endif
    return true;
  }

  if (m_config.enableConsole) {
    fprintf(stderr, "[ERROR] Failed to open log file: %s\n",
            fileName.toLocal8Bit().constData());
  }

  m_currentLogFile.reset();
  return false;
}

void QLogger::closeLogFile() {
  if (m_logStream) {
    m_logStream->flush();
    m_logStream.reset();
  }
  if (m_currentLogFile) {
    m_currentLogFile->close();
    m_currentLogFile.reset();
  }
}

void QLogger::writeLogEntry(const LogEntry& entry) {
  if (!m_logStream) {
    return;
  }

  *m_logStream << formatLogMessage(entry, m_config) << '\n';
  m_logStream->flush();

  // 检查文件大小，需要时进行轮转
  if (m_currentLogFile && m_currentLogFile->size() > m_config.maxFileSize) {
    rotateLogFile();
  }
}

void QLogger::flushPendingLogs() {
  if (!m_logStream) {
    m_logQueue.clear();
    return;
  }

  while (!m_logQueue.isEmpty()) {
    const LogEntry entry = m_logQueue.dequeue();
    *m_logStream << formatLogMessage(entry, m_config) << '\n';
  }

  m_logStream->flush();

  // 检查文件大小，需要时进行轮转
  if (m_currentLogFile && m_currentLogFile->size() > m_config.maxFileSize) {
    rotateLogFile();
  }
}

void QLogger::rotateLogFile() {
  if (!m_currentLogFile) {
    return;
  }

  const QString currentFileName = m_currentLogFile->fileName();
  closeLogFile();

  QDir dir(logDirAbsolute());
  const QString baseName = QString("%1_%2").arg(
      m_config.logFileName,
      QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"));

  QString rotatedName = dir.absoluteFilePath(baseName + ".log");
  int suffix = 1;
  while (QFile::exists(rotatedName)) {
    rotatedName =
        dir.absoluteFilePath(QString("%1_%2.log").arg(baseName).arg(suffix++));
  }

  // 重命名当前文件，失败时保持原文件继续追加
  if (!QFile::rename(currentFileName, rotatedName)) {
    if (m_config.enableConsole) {
      fprintf(stderr, "[ERROR] Failed to rotate log file: %s\n",
              currentFileName.toLocal8Bit().constData());
    }
  }

  // 清理旧文件
  cleanupOldFiles();

  // 创建新文件
  openLogFile();
}

void QLogger::cleanupOldFiles() {
  QDir logDir(logDirAbsolute());
  const QString pattern = QString("%1_*.log").arg(m_config.logFileName);
  QStringList logFiles =
      logDir.entryList(QStringList() << pattern, QDir::Files, QDir::Time);

  // 当前活动文件永远不能被清理
  if (m_currentLogFile) {
    logFiles.removeAll(QFileInfo(m_currentLogFile->fileName()).fileName());
  }

  // 保留最新的几个轮转文件，删除多余的
  while (logFiles.size() > m_config.maxFileCount) {
    const QString oldestFile = logFiles.takeLast();
    logDir.remove(oldestFile);
  }
}

void QLogger::setupAsyncTimer(const LogConfig& config) {
  if (config.asyncMode) {
    if (!m_flushTimer) {
      m_flushTimer = new QTimer(this);
      connect(m_flushTimer, &QTimer::timeout, this, &QLogger::onFlushTimer);
    }
    m_flushTimer->start(config.flushInterval);
  } else if (m_flushTimer) {
    m_flushTimer->stop();
  }
}
