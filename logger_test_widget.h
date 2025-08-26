#ifndef LOGGER_TEST_WIDGET_H
#define LOGGER_TEST_WIDGET_H

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

class LoggerTestWidget : public QWidget {
  Q_OBJECT

 public:
  explicit LoggerTestWidget(QWidget* parent = nullptr);

  ~LoggerTestWidget();

 private slots:
  void onDebugClicked();

  void onInfoClicked();

  void onWarningClicked();

  void onErrorClicked();

  void onStressTestClicked();

  void onFlushClicked();

  void onRotateClicked();

  void onConfigChanged();

  void generateTestLogs();

 private:
  void setupUI();

  void setupConnections();

  void showEvent(QShowEvent* event);

 private:
  // UI 组件
  QPushButton* m_debugButton;
  QPushButton* m_infoButton;
  QPushButton* m_warningButton;
  QPushButton* m_errorButton;
  QPushButton* m_stressButton;
  QPushButton* m_flushButton;
  QPushButton* m_rotateButton;

  QLineEdit* m_logDirEdit;
  QLineEdit* m_fileNameEdit;
  QSpinBox* m_fileSizeSpinBox;
  QSpinBox* m_fileCountSpinBox;
  QSpinBox* m_flushIntervalSpinBox;
  QComboBox* m_logLevelCombo;

  QCheckBox* m_consoleCheckBox;
  QCheckBox* m_timestampCheckBox;
  QCheckBox* m_threadIdCheckBox;
  QCheckBox* m_asyncCheckBox;

  QLabel* m_statusLabel;

  QTimer* m_testTimer;
  bool m_stressTestRunning = false;
};

// #include "LoggerExample.moc"

#endif  // LOGGER_TEST_WIDGET_H
