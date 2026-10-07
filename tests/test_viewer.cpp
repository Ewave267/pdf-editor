// SPDX-License-Identifier: GPL-3.0-only
#include "pdf/PdfDocument.h"
#include "ui/PdfPageItem.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>
#include <cmath>
#include <csignal>

namespace
{
QUrl fixture(const QString& relative)
{
    return QUrl::fromLocalFile(QString(TEST_ROOT) + "/tests/pdfs/" + relative);
}
QQuickItem* item(QQuickItem* root, const QString& name)
{
    if (root->objectName() == name)
        return root;
    for (auto* child : root->childItems())
        if (auto* found = item(child, name))
            return found;
    return nullptr;
}
} // namespace

class ViewerTests : public QObject
{
    Q_OBJECT
  private slots:
    void documentRenderingAndLifecycle()
    {
        PdfDocument document;
        document.open(fixture("normal/multi-page.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready() || !document.error().isEmpty(), 15000);
        QVERIFY2(document.ready(), qPrintable(document.error()));
        QCOMPARE(document.pageCount(), 3);
        QCOMPARE(document.formType(), "PDF");
        const qint64 firstPid = document.workerPid();
        QVERIFY(firstPid > 0);
        const QList<QColor> colors{QColor::fromRgbF(0.2, 0.45, 0.8),
                                   QColor::fromRgbF(0.2, 0.65, 0.4),
                                   QColor::fromRgbF(0.9, 0.5, 0.16)};
        for (int page = 0; page < 3; ++page)
        {
            QSignalSpy images(&document, &PdfDocument::rendered);
            const auto id = document.requestRender(page, 600);
            QTRY_VERIFY_WITH_TIMEOUT(!images.isEmpty(), 10000);
            QCOMPARE(images.first()[0].toULongLong(), id);
            const auto image = qvariant_cast<QImage>(images.first()[1]);
            QCOMPARE(image.width(), 600);
            const auto expected = document.pages()[page].toMap();
            QCOMPARE(image.height(),
                     static_cast<int>(std::ceil(600 * expected["height"].toDouble() /
                                                expected["width"].toDouble())));
            const auto pixel = image.pixelColor(image.width() / 2, image.height() / 2);
            QVERIFY(std::abs(pixel.red() - colors[page].red()) <= 1);
            QVERIFY(std::abs(pixel.green() - colors[page].green()) <= 1);
            QVERIFY(std::abs(pixel.blue() - colors[page].blue()) <= 1);
            // Thumbnail and full-page paths must select the same page.
            images.clear();
            document.requestRender(page, 120);
            QTRY_VERIFY_WITH_TIMEOUT(!images.isEmpty(), 10000);
            const auto thumbnail = qvariant_cast<QImage>(images.first()[1]);
            QCOMPARE(thumbnail.pixelColor(thumbnail.width() / 2, thumbnail.height() / 2), pixel);
        }
        document.open(fixture("normal/single-page.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready() || !document.error().isEmpty(), 15000);
        QVERIFY2(document.ready(), qPrintable(document.error()));
        QCOMPARE(document.pageCount(), 1);
        QVERIFY(document.workerPid() != firstPid);
        QCOMPARE(kill(firstPid, 0), -1);
        document.close();
        QCOMPARE(document.pageCount(), 0);
        QCOMPARE(document.workerPid(), 0);
        QVERIFY(!document.ready());
    }
    void invalidAndXfaDocuments()
    {
        PdfDocument document;
        document.open(fixture("malformed/not-a-pdf.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(!document.error().isEmpty(), 15000);
        QVERIFY(!document.ready());
        QCOMPARE(document.workerPid(), 0);
        document.open(fixture("xfa-javascript/calculation.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready() || !document.error().isEmpty(), 15000);
        QVERIFY2(document.ready(), qPrintable(document.error()));
        QCOMPARE(document.formType(), "XFA");
        QSignalSpy images(&document, &PdfDocument::rendered);
        document.requestRender(0, 612);
        QTRY_VERIFY_WITH_TIMEOUT(!images.isEmpty(), 10000);
        const auto image = qvariant_cast<QImage>(images.first()[1]);
        QCOMPARE(image.size(), QSize(612, 792));
        QVERIFY(image.pixelColor(72, 72) != Qt::white);
        const auto pid = document.workerPid();
        document.open(QUrl("https://example.invalid/no.pdf"));
        QVERIFY(!document.ready());
        QVERIFY(document.error().contains("readable PDF"));
        QCOMPARE(document.workerPid(), 0);
        QCOMPARE(kill(pid, 0), -1);
        document.close();
    }
    void closeDuringRenderAndMissingSandbox()
    {
        PdfDocument document;
        const auto previousPath = qgetenv("PATH");
        qputenv("PATH", "/nonexistent");
        document.open(fixture("normal/single-page.pdf"));
        qputenv("PATH", previousPath);
        QVERIFY(!document.error().isEmpty());
        QVERIFY(!document.ready());
        QCOMPARE(document.workerPid(), 0);
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            document.open(fixture("normal/multi-page.pdf"));
            QTRY_VERIFY_WITH_TIMEOUT(document.ready() || !document.error().isEmpty(), 15000);
            QVERIFY2(document.ready(), qPrintable(document.error()));
            const auto pid = document.workerPid();
            document.requestRender(2, 2400);
            document.close();
            QCOMPARE(document.workerPid(), 0);
            QCOMPARE(kill(pid, 0), -1);
            QCoreApplication::processEvents();
            QVERIFY(!document.ready());
        }
    }
    void qmlControlsAndScrolling()
    {
        PdfDocument document;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("pdfDocument", &document);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto click = [window](const QString& name)
        {
            auto* control = item(window->contentItem(), name);
            if (!control)
                return false;
            QTest::mouseClick(
                window, Qt::LeftButton, Qt::NoModifier,
                control->mapToScene(QPointF(control->width() / 2, control->height() / 2))
                    .toPoint());
            return true;
        };
        auto* dialog = window->findChild<QObject*>("openDialog");
        QVERIFY(dialog);
        QVERIFY(dialog->setProperty(
            "currentFolder", QUrl::fromLocalFile(QString(TEST_ROOT) + "/tests/pdfs/normal")));
        QVERIFY(dialog->setProperty("selectedFile", fixture("normal/multi-page.pdf")));
        QVERIFY(click("openButton"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTRY_COMPARE(dialog->property("selectedFile").toUrl(), fixture("normal/multi-page.pdf"));
        QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready() || !document.error().isEmpty(), 15000);
        QVERIFY2(document.ready(), qPrintable(document.error()));
        QTRY_VERIFY(item(window->contentItem(), "page0"));
        auto* page0 = qobject_cast<PdfPageItem*>(item(window->contentItem(), "page0"));
        QTRY_VERIFY_WITH_TIMEOUT(page0->rendered(), 10000);
        QVERIFY(page0->height() <= window->height());
        const double fit = document.zoom();
        QVERIFY(click("zoomInButton"));
        QTRY_VERIFY(document.zoom() > fit);
        QVERIFY(click("zoomOutButton"));
        QTRY_VERIFY(document.zoom() < fit * 1.2);
        QVERIFY(click("fitButton"));
        QVERIFY(document.fitting());
        QVERIFY(std::abs(document.zoom() - fit) < 0.001);
        QVERIFY(click("nextButton"));
        QTRY_COMPARE(document.currentPage(), 2);
        auto* view = item(window->contentItem(), "pageView");
        QVERIFY(view);
        const auto y = view->property("contentY").toDouble();
        QVERIFY(y > 0);
        auto* number = item(window->contentItem(), "pageNumber");
        QVERIFY(number);
        auto* numberInput =
            qobject_cast<QQuickItem*>(number->property("contentItem").value<QObject*>());
        QVERIFY(numberInput);
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        numberInput->forceActiveFocus();
        QTRY_VERIFY(numberInput->hasActiveFocus());
        QVERIFY(QMetaObject::invokeMethod(numberInput, "selectAll"));
        QTest::keyClick(window, Qt::Key_1);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(document.currentPage(), 1);
        QVERIFY(click("thumbnail2"));
        QTRY_COMPARE(document.currentPage(), 3);
        QTRY_VERIFY(item(window->contentItem(), "page2"));
        auto* page2 = qobject_cast<PdfPageItem*>(item(window->contentItem(), "page2"));
        QTRY_VERIFY_WITH_TIMEOUT(page2->rendered(), 10000);
        QVERIFY(view->property("contentY").toDouble() > y);
        // Scroll back without navigation controls; the displayed page must track it.
        view->setProperty("contentY", 0.0);
        QTRY_COMPARE(document.currentPage(), 1);
        QVERIFY(page0->rendered());
        if (qEnvironmentVariableIsSet("PDF_VIEWER_SCREENSHOT"))
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("PDF_VIEWER_SCREENSHOT")));
        QVERIFY(click("closeButton"));
        QTRY_VERIFY(!document.ready());
        QCOMPARE(document.workerPid(), 0);
        QCOMPARE(document.pageCount(), 0);
        window->close();
    }
};
int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<PdfPageItem>("PdfEditor", 1, 0, "PdfPage");
    qmlRegisterUncreatableType<PdfDocument>("PdfEditor", 1, 0, "PdfDocument",
                                            "Provided by the application");
    ViewerTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_viewer.moc"
