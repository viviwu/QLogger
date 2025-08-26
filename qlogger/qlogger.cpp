#include "qlogger.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>
#include <memory>

// 初始化日志系统
bool QLogger::initialize(const LogConfig& config) {
  // 防止重复初始化
  if (m_initialized) {
    return true;
  }

  // 先设置配置和标志，避免在加锁状态下调用其他可能产生日志的函数
  m_config = config;

  // 设置日志目录（不加锁，避免死锁）
  if (!setupLogDirectory()) {
    return false;
  }

  // 打开日志文件（不加锁，避免死锁）
  if (!openLogFile()) {
    return false;
  }

  // 设置初始化标志
  m_initialized = true;

  // 安装消息处理器（此时已经可以安全处理日志）
  qInstallMessageHandler(messageHandler);

  // 如果启用异步模式，设置定时器
  if (m_config.asyncMode) {
    setupAsyncTimer();
  }

  // 使用日志系统输出初始化成功消息（现在是安全的）
  LOG_INFO("QLogger initialized successfully");

  return true;
}

// 设置日志配置
void QLogger::setConfig(const LogConfig& config) {
  if (!m_initialized) {
    m_config = config;
    return;
  }

  // 临时禁用日志处理器，避免在重新配置过程中的死锁
  qInstallMessageHandler(nullptr);

  {
    QMutexLocker locker(&m_mutex);
    m_config = config;
    closeLogFile();
    setupLogDirectory();
    openLogFile();
  }

  // 重新安装消息处理器
  qInstallMessageHandler(messageHandler);
}

// 获取当前配置
LogConfig QLogger::getConfig() const {
  QMutexLocker locker(&m_mutex);
  return m_config;
}

// 立即刷新日志到文件
void QLogger::flush() {
  QMutexLocker locker(&m_mutex);
  flushPendingLogs();
}

// 强制日志轮转
void QLogger::rotate() {
  QMutexLocker locker(&m_mutex);
  if (m_initialized) {
    rotateLogFile();
  }
}

// 清理旧日志文件
void QLogger::cleanup() {
  QMutexLocker locker(&m_mutex);
  if (m_initialized) {
    cleanupOldFiles();
  }
}

// 关闭日志系统
void QLogger::shutdown() {
  if (!m_initialized) {
    return;
  }

  // 先恢复默认日志处理器，避免在清理过程中继续处理日志
  qInstallMessageHandler(nullptr);

  QMutexLocker locker(&m_mutex);

  if (m_flushTimer) {
    m_flushTimer->stop();
    m_flushTimer->deleteLater();
    m_flushTimer = nullptr;
  }

  flushPendingLogs();
  closeLogFile();
  m_initialized = false;

  if (m_config.enableConsole) {
    fprintf(stdout, "[INFO] QLogger shutdown completed\n");
    fflush(stdout);
  }
}

// private slots:
void QLogger::onFlushTimer() {
  if (m_initialized) {
    QMutexLocker locker(&m_mutex);
    flushPendingLogs();
  }
}

// private:
QLogger::QLogger() : m_initialized(false), m_flushTimer(nullptr) {
  // 确保在应用程序退出时自动清理
  if (qApp) {
    connect(qApp, &QApplication::aboutToQuit, this, &QLogger::shutdown);
  }
}

QLogger::~QLogger() { shutdown(); }

void QLogger::handleMessage(QtMsgType type, const QMessageLogContext& context,
                            const QString& msg) {
  // 防止日志系统自己的消息造成递归
  if (msg.contains("QLogger") &&
      (msg.contains("initialized") || msg.contains("shutdown"))) {
    return;
  }

  if (!shouldLog(type)) {
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

  // 输出到控制台（不需要加锁）
  if (m_config.enableConsole) {
    outputToConsole(entry);
  }

  // 处理文件输出（需要加锁，但范围要小）
  if (m_config.asyncMode) {
    // 异步模式：添加到队列
    {
      QMutexLocker locker(&m_mutex);
      m_logQueue.enqueue(entry);

      // 如果队列过长，立即刷新
      if (m_logQueue.size() > 100) {
        flushPendingLogs();
      }
    }
  } else {
    // 同步模式：直接写入文件
    {
      QMutexLocker locker(&m_mutex);
      writeLogEntry(entry);
    }
  }
}

bool QLogger::shouldLog(QtMsgType type) const {
  LogLevel level = static_cast<LogLevel>(type);
  return level >= m_config.minLevel;
}

void QLogger::outputToConsole(const LogEntry& entry) {
  QString formattedMsg = formatLogMessage(entry);

  // 直接使用C风格输出，避免Qt的递归问题
  if (entry.type >= QtCriticalMsg) {
    fprintf(stderr, "%s\n", formattedMsg.toLocal8Bit().constData());
    fflush(stderr);
  } else {
    fprintf(stdout, "%s\n", formattedMsg.toLocal8Bit().constData());
    fflush(stdout);
  }
}

QString QLogger::formatLogMessage(const LogEntry& entry) {
  QString result;
  QTextStream stream(&result);

  // 时间戳
  if (m_config.enableTimestamp) {
    stream << "[" << entry.timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz")
           << "] ";
  }

  // 日志级别
  stream << "[" << levelToString(entry.type) << "] ";

  // 线程ID
  if (m_config.enableThreadId) {
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

bool QLogger::setupLogDirectory() {
  QDir dir;
  QString fullPath = dir.absoluteFilePath(m_config.logDir);

  if (!dir.exists(fullPath)) {
    if (!dir.mkpath(fullPath)) {
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
  QString fileName =
      QString("%1/%2_%3.log")
          .arg(m_config.logDir)
          .arg(m_config.logFileName)
          .arg(QDateTime::currentDateTime().toString("yyyyMMdd"));

  m_currentLogFile = std::make_unique<QFile>(fileName);

  if (m_currentLogFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
    m_logStream = std::make_unique<QTextStream>(m_currentLogFile.get());
    m_logStream->setEncoding(QStringConverter::Utf8);
    return true;
  } else {
    if (m_config.enableConsole) {
      fprintf(stderr, "[ERROR] Failed to open log file: %s\n",
              fileName.toLocal8Bit().constData());
    }
    return false;
  }
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

  QString formattedMsg = formatLogMessage(entry);
  *m_logStream << formattedMsg << Qt::endl;
  m_logStream->flush();

  // 检查文件大小，需要时进行轮转
  if (m_currentLogFile && m_currentLogFile->size() > m_config.maxFileSize) {
    rotateLogFile();
  }
}

void QLogger::flushPendingLogs() {
  if (!m_logStream) {
    return;
  }

  while (!m_logQueue.isEmpty()) {
    LogEntry entry = m_logQueue.dequeue();
    QString formattedMsg = formatLogMessage(entry);
    *m_logStream << formattedMsg << Qt::endl;
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

  QString currentFileName = m_currentLogFile->fileName();
  closeLogFile();

  QString baseName =
      QString("%1/%2_%3")
          .arg(m_config.logDir)
          .arg(m_config.logFileName)
          .arg(QDateTime::currentDateTime().toString("yyyyMMdd"));

  // 重命名当前文件
  QString timestamp = QDateTime::currentDateTime().toString("hhmmss");
  QString rotatedName = QString("%1_%2.log").arg(baseName).arg(timestamp);

  QFile::rename(currentFileName, rotatedName);

  // 清理旧文件
  cleanupOldFiles();

  // 创建新文件
  openLogFile();
}

void QLogger::cleanupOldFiles() {
  QDir logDir(m_config.logDir);
  QString pattern = QString("%1_*.log").arg(m_config.logFileName);
  QStringList logFiles =
      logDir.entryList(QStringList() << pattern, QDir::Files, QDir::Time);

  // 保留最新的几个文件，删除多余的
  while (logFiles.size() > m_config.maxFileCount) {
    QString oldestFile = logFiles.takeLast();
    logDir.remove(oldestFile);
  }
}

void QLogger::setupAsyncTimer() {
  if (!m_flushTimer) {
    m_flushTimer = new QTimer(this);
    connect(m_flushTimer, &QTimer::timeout, this, &QLogger::onFlushTimer);
  }
  m_flushTimer->start(m_config.flushInterval);
}
