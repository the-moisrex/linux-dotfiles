#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[]) {
  QCoreApplication a(argc, argv);

  int res = a.exec();
  qInfo() << "Done with exit code:" << res;
  return res;
}
