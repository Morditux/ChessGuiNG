#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QTranslator>

#include "chessboard.h"
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ChessGui"));
    QApplication::setApplicationDisplayName(QStringLiteral("ChessGui"));
    QApplication::setDesktopFileName(QStringLiteral("chessgui"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/ChessGui/icons/chessgui.png")));

    // Load a translation matching the system locale when one is embedded in
    // the resources (translations/chessgui_<locale>.qm). The UI falls back
    // to the English source strings when no match is found.
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        auto *translator = new QTranslator;
        if (translator->load(QLocale(locale), QStringLiteral("chessgui"),
                             QStringLiteral("_"), QStringLiteral(":/translations"))) {
            QApplication::installTranslator(translator);
            break;
        }
        delete translator;
    }

    MainWindow w;
    w.show();
    return QApplication::exec();
}
