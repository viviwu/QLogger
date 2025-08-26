#include "logger_test_widget.h"

#include <QDir>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "qlogger/qlogger.h"

// 定义QLogger默认分类
Q_LOGGING_CATEGORY(qlogger_default, "app")
// 自定义日志分类
Q_LOGGING_CATEGORY(ui, "ui")
Q_LOGGING_CATEGORY(network, "network")
Q_LOGGING_CATEGORY(database, "database")

LoggerTestWidget::LoggerTestWidget(QWidget* parent) : QWidget(parent) {
  setupUI();
  setupConnections();

  ///////////////////////////////////////////////
  // 动态设置日志过滤规则
  QLoggingCategory::setFilterRules(
      "app.debug=false\n"
      "network.info=true\n"
  );
  LOG_DEBUG("这条 app.debug 日志不会输出");
  LOG_NETWORK_INFO("这条 network.info 日志会输出");

  // QT_LOGGING_RULES 也可以通过环境变量或代码动态设置
  // export QT_LOGGING_RULES="app.debug=false;network.info=true"
  // ./ your_app configLogger();

  ///////////////////////////////////////////////

  // 启动测试定时器
  m_testTimer = new QTimer(this);
  connect(m_testTimer, &QTimer::timeout, this,
          &LoggerTestWidget::generateTestLogs);
}

LoggerTestWidget::~LoggerTestWidget() { shutdownLogger(); }

void LoggerTestWidget::onDebugClicked() {
  qCDebug(ui) << "Debug button clicked at"
              << QDateTime::currentDateTime().toString();
  LOG_DEBUG("This is a debug message from macro");
}

void LoggerTestWidget::onInfoClicked() {
  qCInfo(ui) << "Info button clicked - application state normal";
  LOG_INFO("Application state updated successfully");
}

void LoggerTestWidget::onWarningClicked() {
  qCWarning(network) << "Warning: Network connection unstable, retrying...";
  LOG_WARNING("Network timeout detected, attempting reconnection");
}

void LoggerTestWidget::onErrorClicked() {
  qCCritical(database) << "Critical: Database connection failed!";
  LOG_CRITICAL("Failed to connect to database server");
}

void LoggerTestWidget::onStressTestClicked() {
  m_stressTestRunning = !m_stressTestRunning;

  if (m_stressTestRunning) {
    m_stressButton->setText("Stop Stress Test");
    m_testTimer->start(10);  // 每10ms生成一条日志
    qCInfo(ui) << "Stress test started";
  } else {
    m_stressButton->setText("Start Stress Test");
    m_testTimer->stop();
    qCInfo(ui) << "Stress test stopped";
  }
}

void LoggerTestWidget::onFlushClicked() {
  flushLogger();
  qCInfo(ui) << "Log flushed manually";
  m_statusLabel->setText("Status: Logs flushed to file");
}

void LoggerTestWidget::onRotateClicked() {
  QLogger::instance().rotate();
  qCInfo(ui) << "Log file rotated manually";
  m_statusLabel->setText("Status: Log file rotated");
}

void LoggerTestWidget::onConfigChanged() {
  LogConfig config;
  config.logDir = m_logDirEdit->text();
  config.logFileName = m_fileNameEdit->text();
  config.maxFileSize = m_fileSizeSpinBox->value() * 1024;
  config.maxFileCount = m_fileCountSpinBox->value();
  config.enableConsole = m_consoleCheckBox->isChecked();
  config.enableTimestamp = m_timestampCheckBox->isChecked();
  config.enableThreadId = m_threadIdCheckBox->isChecked();
  config.asyncMode = m_asyncCheckBox->isChecked();
  config.flushInterval = m_flushIntervalSpinBox->value();

  // 设置日志级别
  QString levelText = m_logLevelCombo->currentText();
  if (levelText == "Debug")
    config.minLevel = LogLevel::Debug;
  else if (levelText == "Info")
    config.minLevel = LogLevel::Info;
  else if (levelText == "Warning")
    config.minLevel = LogLevel::Warning;
  else if (levelText == "Critical")
    config.minLevel = LogLevel::Critical;

  QLogger::instance().setConfig(config);
  qCInfo(ui) << "Logger configuration updated";
  m_statusLabel->setText("Status: Configuration updated");
}

void LoggerTestWidget::generateTestLogs() {
  static int counter = 0;
  counter++;

  // 随机生成不同类型的日志
  int logType = counter % 4;
  switch (logType) {
    case 0:
      qCDebug(ui) << "Stress test debug message #" << counter;
      break;
    case 1:
      qCInfo(network) << "Processing request #" << counter
                      << "- bytes transferred:" << (counter * 1024);
      break;
    case 2:
      if (counter % 10 == 0) {
        qCWarning(database)
            << "Database query took longer than expected:" << (counter * 5)
            << "ms";
      }
      break;
    case 3:
      if (counter % 50 == 0) {
        qCCritical(ui) << "Simulated error condition #" << (counter / 50);
      }
      break;
  }
}

void LoggerTestWidget::setupUI() {
  auto* mainLayout = new QVBoxLayout(this);

  // 标题
  auto* titleLabel = new QLabel("QLogger Test Application");
  titleLabel->setStyleSheet(
      "font-size: 18px; font-weight: bold; margin: 10px;");
  titleLabel->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(titleLabel);

  // 测试按钮组
  auto* buttonGroup = new QGroupBox("Log Test Buttons");
  auto* buttonLayout = new QHBoxLayout(buttonGroup);

  m_debugButton = new QPushButton("Debug Log");
  m_infoButton = new QPushButton("Info Log");
  m_warningButton = new QPushButton("Warning Log");
  m_errorButton = new QPushButton("Error Log");

  buttonLayout->addWidget(m_debugButton);
  buttonLayout->addWidget(m_infoButton);
  buttonLayout->addWidget(m_warningButton);
  buttonLayout->addWidget(m_errorButton);

  mainLayout->addWidget(buttonGroup);

  // 控制按钮组
  auto* controlGroup = new QGroupBox("Logger Controls");
  auto* controlLayout = new QHBoxLayout(controlGroup);

  m_stressButton = new QPushButton("Start Stress Test");
  m_flushButton = new QPushButton("Flush Logs");
  m_rotateButton = new QPushButton("Rotate Log File");

  controlLayout->addWidget(m_stressButton);
  controlLayout->addWidget(m_flushButton);
  controlLayout->addWidget(m_rotateButton);

  mainLayout->addWidget(controlGroup);

  // 配置组
  auto* configGroup = new QGroupBox("Logger Configuration");
  auto* configLayout = new QVBoxLayout(configGroup);

  // 第一行配置
  auto* row1Layout = new QHBoxLayout();
  row1Layout->addWidget(new QLabel("Log Dir:"));
  m_logDirEdit = new QLineEdit("test_logs");
  row1Layout->addWidget(m_logDirEdit);

  row1Layout->addWidget(new QLabel("File Name:"));
  m_fileNameEdit = new QLineEdit("test_app");
  row1Layout->addWidget(m_fileNameEdit);

  row1Layout->addWidget(new QLabel("Min Level:"));
  m_logLevelCombo = new QComboBox();
  m_logLevelCombo->addItems({"Debug", "Info", "Warning", "Critical"});
  row1Layout->addWidget(m_logLevelCombo);

  configLayout->addLayout(row1Layout);

  // 第二行配置
  auto* row2Layout = new QHBoxLayout();
  row2Layout->addWidget(new QLabel("Max File Size (KB):"));
  m_fileSizeSpinBox = new QSpinBox();
  m_fileSizeSpinBox->setRange(1, 10240);
  m_fileSizeSpinBox->setValue(1024);
  row2Layout->addWidget(m_fileSizeSpinBox);

  row2Layout->addWidget(new QLabel("Max File Count:"));
  m_fileCountSpinBox = new QSpinBox();
  m_fileCountSpinBox->setRange(1, 10);
  m_fileCountSpinBox->setValue(3);
  row2Layout->addWidget(m_fileCountSpinBox);

  row2Layout->addWidget(new QLabel("Flush Interval (ms):"));
  m_flushIntervalSpinBox = new QSpinBox();
  m_flushIntervalSpinBox->setRange(100, 5000);
  m_flushIntervalSpinBox->setValue(500);
  row2Layout->addWidget(m_flushIntervalSpinBox);

  configLayout->addLayout(row2Layout);

  // 第三行配置（复选框）
  auto* row3Layout = new QHBoxLayout();
  m_consoleCheckBox = new QCheckBox("Console Output");
  m_consoleCheckBox->setChecked(true);
  m_timestampCheckBox = new QCheckBox("Timestamp");
  m_timestampCheckBox->setChecked(true);
  m_threadIdCheckBox = new QCheckBox("Thread ID");
  m_threadIdCheckBox->setChecked(true);
  m_asyncCheckBox = new QCheckBox("Async Mode");
  m_asyncCheckBox->setChecked(true);

  row3Layout->addWidget(m_consoleCheckBox);
  row3Layout->addWidget(m_timestampCheckBox);
  row3Layout->addWidget(m_threadIdCheckBox);
  row3Layout->addWidget(m_asyncCheckBox);

  configLayout->addLayout(row3Layout);

  auto* applyButton = new QPushButton("Apply Configuration");
  configLayout->addWidget(applyButton);
  connect(applyButton, &QPushButton::clicked, this,
          &LoggerTestWidget::onConfigChanged);

  mainLayout->addWidget(configGroup);

  // 状态标签
  m_statusLabel = new QLabel("Status: Logger initialized");
  m_statusLabel->setStyleSheet(
      "background-color: #e8f4f8; padding: 5px; border: 1px solid #ccc;");
  mainLayout->addWidget(m_statusLabel);

  // 说明文本
  auto* infoText = new QTextEdit();
  infoText->setReadOnly(true);
  infoText->setMaximumHeight(120);
  infoText->setPlainText(
      "QLogger Test Application\n\n"
      "功能说明：\n"
      "• 点击不同按钮生成各级别日志消息\n"
      "• 压力测试会快速生成大量日志以测试性能\n"
      "• 支持实时配置更改\n"
      "• 日志会同时输出到控制台和文件\n"
      "• 支持日志文件轮转和清理");
  mainLayout->addWidget(infoText);

  setWindowTitle("QLogger Test Application");
  resize(800, 600);
}

void LoggerTestWidget::setupConnections() {
  // Connect button click to slot using traditional syntax
  connect(m_debugButton, &QPushButton::clicked, this,
          &LoggerTestWidget::onDebugClicked);
  connect(m_infoButton, &QPushButton::clicked, this,
          &LoggerTestWidget::onInfoClicked);
  connect(m_warningButton, &QPushButton::clicked, this,
          &LoggerTestWidget::onWarningClicked);
  connect(m_errorButton, &QPushButton::clicked, this,
          &LoggerTestWidget::onErrorClicked);
  connect(m_stressButton, &QPushButton::clicked, this,
          &LoggerTestWidget::onStressTestClicked);
  connect(m_flushButton, &QPushButton::clicked, this,
          &LoggerTestWidget::onFlushClicked);
  connect(m_rotateButton, &QPushButton::clicked, this,
          &LoggerTestWidget::onRotateClicked);
}

void LoggerTestWidget:: configLogger() {
  // Use QStandardPaths to get the Logs location
  QString logsPath =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  // On macOS: ~/Library/Application Support/MyQtApp
  // We'll append "/logs" to follow convention

  QString logDir = logsPath + "/logs";
  QDir dir;
  if (!dir.exists(logDir)) {
    dir.mkpath(logDir);  // Create logs directory if it doesn't exist
  }

  // 初始化日志系统
  LogConfig config;
  config.logDir = logsPath;
  config.logFileName = "test_app";
  config.maxFileSize = 1024 * 1024;  // 1MB for testing
  config.maxFileCount = 3;
  config.enableConsole = true;
  config.enableTimestamp = true;
  config.enableThreadId = true;
  config.asyncMode = true;
  config.flushInterval = 500;

  initLogger(config);

  LOG_DEBUG(logDir);
}

void LoggerTestWidget::showEvent(QShowEvent* event) {

}
