// SPDX-License-Identifier: GPL-3.0-only
#include "pdf/PdfDocument.h"
#include "ui/AddedOverlay.h"
#include "ui/FormInput.h"
#include "ui/PdfPageItem.h"
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <iostream>
#include <cstdio>
#ifdef Q_OS_WIN
#include <QFile>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <windows.h>

namespace {
QString startupLog;
QMutex startupLogMutex;
void logWindowsMessage(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    const auto encoded = message.toUtf8();
    std::fprintf(stderr, "%s\n", encoded.constData());
    {
        QMutexLocker lock(&startupLogMutex);
        QFile file(startupLog);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append))
            file.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + " "
                        + QString::number(type) + " " + message + "\n").toUtf8());
    }
    if (type == QtFatalMsg && qEnvironmentVariable("QT_QPA_PLATFORM") != "offscreen") {
        const QString details = message + "\n\nStartup log: " + startupLog;
        MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(details.utf16()),
                    L"PDF Editor startup failed", MB_OK | MB_ICONERROR);
    }
}
void initializeStartupLog()
{
    QString directory = qEnvironmentVariable("LOCALAPPDATA");
    if (directory.isEmpty()) directory = QDir::tempPath();
    directory += "/PDF Editor";
    QDir().mkpath(directory);
    startupLog = directory + "/startup.log";
    // Rotate once per launch to keep diagnostics bounded.
    QFile::remove(startupLog + ".previous");
    QFile::rename(startupLog, startupLog + ".previous");
    qInstallMessageHandler(logWindowsMessage);
    qInfo("Starting PDF Editor");
}
}
#endif

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--version")
    {
        std::cout << "PDF Form Editor 0.1.0\n";
        return 0;
    }
#ifdef Q_OS_WIN
    initializeStartupLog();
#endif
    QGuiApplication app(argc, argv);
    app.setApplicationName("PDF Form Editor");
    app.setOrganizationName("PDF Form Editor");
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<FormInput>("PdfEditor", 1, 0, "FormInput");
    qmlRegisterType<AddedOverlay>("PdfEditor", 1, 0, "AddedOverlay");
    qmlRegisterUncreatableType<AddedContent>("PdfEditor", 1, 0, "AddedContent",
                                             "Owned by document");
    qmlRegisterType<PdfPageItem>("PdfEditor", 1, 0, "PdfPage");
    qmlRegisterUncreatableType<PdfDocument>("PdfEditor", 1, 0, "PdfDocument",
                                            "Provided by the application");
    PdfDocument document;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("pdfDocument", &document);
    const QString initialFolder = QDir::homePath();
    engine.rootContext()->setContextProperty("startupFolder", QUrl::fromLocalFile(initialFolder));
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
#ifdef Q_OS_WIN
        const QString details = "The application interface could not be loaded.\n\nStartup log: " + startupLog;
        if (qEnvironmentVariable("QT_QPA_PLATFORM") != "offscreen")
            MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(details.utf16()),
                    L"PDF Editor startup failed", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()))
        window->showFullScreen();
    const auto arguments = app.arguments();
    if (arguments.size() == 2)
        document.open(QUrl::fromLocalFile(arguments[1]));
    return app.exec();
}
