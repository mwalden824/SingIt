#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("3Point");
    QCoreApplication::setApplicationName("SingIt");
    MainWindow w;
    w.show();
    return QApplication::exec();
}

