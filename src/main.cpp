#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName("RFdudeSmithChartV2");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCoreApplication::setOrganizationName("RFdude");
    QCommandLineParser parser;
    parser.setApplicationDescription("Cross-platform RF Smith chart and network tuning");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("project", "V2 JSON or legacy .lls project to open.", "[project]");
    parser.process(application);

    MainWindow window;
    const auto files = parser.positionalArguments();
    if (files.size() > 1) parser.showHelp(1);
    if (!files.isEmpty() && !window.openDocument(files.front())) return 1;
    window.show();

    return application.exec();
}
