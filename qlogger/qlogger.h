#ifndef Q_LOGGER_H
#define Q_LOGGER_H

#include <QDateTime>
#include <QFile>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QQueue>
#include <QThread>
#include <QTimer>

// 定义日志级别枚举
enum class LogLevel {
  Debug = 0,
  Info = 1,
  Warning = 2,
  Critical = 3,
  Fatal = 4
};

// 日志配置结构
struct LogConfig {
  QString logDir = "logs";              // 日志目录
  QString logFileName = "app_log";      // 日志文件名前缀
  int maxFileSize = 10 * 1024 * 1024;   // 最大文件大小(10MB)
  int maxFileCount = 5;                 // 最大文件数量
  LogLevel minLevel = LogLevel::Debug;  // 最小日志级别
  bool enableConsole = true;            // 是否同时输出到控制台
  bool enableTimestamp = true;          // 是否包含时间戳
  bool enableThreadId = false;          // 是否包含线程ID
  bool asyncMode = false;               // 默认同步模式，避免复杂性
  int flushInterval = 1000;             // 刷新间隔(ms)
};

// 日志条目结构
struct LogEntry {
  QtMsgType type;
  QString message;
  QString category;
  QString file;
  QString function;
  int line;
  QDateTime timestamp;
  Qt::HANDLE threadId;

  LogEntry() : type(QtDebugMsg), line(0), threadId(nullptr) {}
};

class QLogger : public QObject {
  Q_OBJECT

 public:
  static QLogger& instance() {
    static QLogger logger;
    return logger;
  }

  // 初始化日志系统
  bool initialize(const LogConfig& config = LogConfig());

  // 设置日志配置
  void setConfig(const LogConfig& config);

  // 获取当前配置
  LogConfig getConfig() const;

  // 立即刷新日志到文件
  void flush();

  // 强制日志轮转
  void rotate();

  // 清理旧日志文件
  void cleanup();

  // 关闭日志系统
  void shutdown();

 private slots:
  void onFlushTimer();

 private:
  QLogger();

  ~QLogger();

  // 禁用拷贝构造和赋值
  QLogger(const QLogger&) = delete;
  QLogger& operator=(const QLogger&) = delete;

  static void messageHandler(QtMsgType type, const QMessageLogContext& context,
                             const QString& msg) {
    QLogger& logger = QLogger::instance();

    // 防止在初始化过程中的递归调用
    if (!logger.m_initialized) {
      return;
    }

    logger.handleMessage(type, context, msg);
  }

  void handleMessage(QtMsgType type, const QMessageLogContext& context,
                     const QString& msg);

  bool shouldLog(QtMsgType type) const;
  void outputToConsole(const LogEntry& entry);

  QString formatLogMessage(const LogEntry& entry);

  QString levelToString(QtMsgType type) const;

  bool setupLogDirectory();

  bool openLogFile();

  void closeLogFile();

  void writeLogEntry(const LogEntry& entry);

  void flushPendingLogs();

  void rotateLogFile();

  void cleanupOldFiles();

  void setupAsyncTimer();

 private:
  mutable QMutex m_mutex;
  LogConfig m_config;
  volatile bool m_initialized;

  std::unique_ptr<QFile> m_currentLogFile;
  std::unique_ptr<QTextStream> m_logStream;
  QQueue<LogEntry> m_logQueue;

  QTimer* m_flushTimer;
};

// 简单的日志分类管理器
class LogCategories {
 public:
  static const QLoggingCategory& app() {
    static const QLoggingCategory category("app");
    return category;
  }

  static const QLoggingCategory& ui() {
    static const QLoggingCategory category("ui");
    return category;
  }

  static const QLoggingCategory& network() {
    static const QLoggingCategory category("network");
    return category;
  }

  static const QLoggingCategory& database() {
    static const QLoggingCategory category("database");
    return category;
  }
};

// 便利的日志宏定义
#define LOG_DEBUG(msg) qCDebug(LogCategories::app()) << msg
#define LOG_INFO(msg) qCInfo(LogCategories::app()) << msg
#define LOG_WARNING(msg) qCWarning(LogCategories::app()) << msg
#define LOG_CRITICAL(msg) qCCritical(LogCategories::app()) << msg

// 分类日志宏
#define LOG_UI_DEBUG(msg) qCDebug(LogCategories::ui()) << msg
#define LOG_UI_INFO(msg) qCInfo(LogCategories::ui()) << msg
#define LOG_NETWORK_DEBUG(msg) qCDebug(LogCategories::network()) << msg
#define LOG_NETWORK_INFO(msg) qCInfo(LogCategories::network()) << msg
#define LOG_DATABASE_DEBUG(msg) qCDebug(LogCategories::database()) << msg
#define LOG_DATABASE_INFO(msg) qCInfo(LogCategories::database()) << msg

// 便利的全局函数
inline bool initLogger(const LogConfig& config = LogConfig()) {
  return QLogger::instance().initialize(config);
}

inline void shutdownLogger() { QLogger::instance().shutdown(); }

inline void flushLogger() { QLogger::instance().flush(); }

#endif  // Q_LOGGER_H
