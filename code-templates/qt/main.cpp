#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[]) {
  QCoreApplication a(argc, argv);

  qInfo() << "Done";
  return a.exec();
}
