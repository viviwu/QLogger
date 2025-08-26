#include <QApplication>

#include "logger_test_widget.h"

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  // 设置应用程序信息
  app.setApplicationName("logger_app");
  app.setApplicationVersion("1.2.3");
  app.setOrganizationName("CSCO");

  // 创建并显示测试窗口
  LoggerTestWidget window;
  window.show();

  // 运行应用程序
  int result = app.exec();

  return result;
}
