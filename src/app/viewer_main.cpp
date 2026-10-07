// SPDX-License-Identifier: GPL-3.0-only
#include "pdf/PdfDocument.h"
#include "ui/AddedOverlay.h"
#include "ui/PdfPageItem.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--version")
    {
        std::cout << "PDF Form Editor 0.1.0\n";
        return 0;
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName("PDF Form Editor");
    app.setOrganizationName("PDF Form Editor");
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<AddedOverlay>("PdfEditor", 1, 0, "AddedOverlay");
    qmlRegisterUncreatableType<AddedContent>("PdfEditor", 1, 0, "AddedContent",
                                             "Owned by document");
    qmlRegisterType<PdfPageItem>("PdfEditor", 1, 0, "PdfPage");
    qmlRegisterUncreatableType<PdfDocument>("PdfEditor", 1, 0, "PdfDocument",
                                            "Provided by the application");
    PdfDocument document;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("pdfDocument", &document);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (argc == 2)
        document.open(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    return app.exec();
}
