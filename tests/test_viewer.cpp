// SPDX-License-Identifier: GPL-3.0-only
#include "pdf/PdfDocument.h"
#include "ui/AddedOverlay.h"
#include "ui/PdfPageItem.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
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
    void addedContentLifecycleAndRendering()
    {
        PdfDocument document;
        QFile source(fixture("normal/multi-page.pdf").toLocalFile());
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QByteArray original = source.readAll();
        document.open(fixture("normal/multi-page.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        auto* content = document.additions();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString imagePath = directory.filePath("signature.png");
        QImage image(80, 40, QImage::Format_ARGB32);
        image.fill(Qt::magenta);
        QVERIFY(image.save(imagePath));
        const QString text = QString::fromUtf8("Hello <b>world</b>\nRésumé — 日本語");
        const int textId = content->addText(0, 30, 40, text);
        const int imageId = content->addImage(1, 30, 40, QUrl::fromLocalFile(imagePath));
        const int signatureId = content->addImage(2, 30, 40, QUrl::fromLocalFile(imagePath), true);
        QVERIFY(textId > 0 && imageId > textId && signatureId > imageId);
        QVERIFY(QFile::remove(imagePath)); // Imported image is owned, independent of the file.
        QCOMPARE(content->count(), 3);
        QCOMPARE(content->object(textId)["text"].toString(), text);
        QCOMPARE(content->object(signatureId)["type"].toString(), "signature");
        QCOMPARE(content->addImage(0, 0, 0, QUrl("https://example.invalid/image.png")), -1);
        QCOMPARE(content->addText(3, 0, 0, "invalid page"), -1);
        QCOMPARE(content->count(), 3);
        const QList<int> ids{textId, imageId, signatureId};
        for (int page = 0; page < 3; ++page)
        {
            const int id = ids[page];
            QCOMPARE(content->hit(page, 35, 45), id);
            content->select(id);
            QVERIFY(content->geometry(id, 60, 80, 100, 50));
            QCOMPARE(content->object(id)["page"].toInt(), page);
            QCOMPARE(content->object(id)["width"].toDouble(), 100.0);
            QCOMPARE(content->hit(page, 65, 85), id);
            QVERIFY(content->hit((page + 1) % 3, 65, 85) != id);
            AddedOverlay overlay;
            overlay.setContent(content);
            overlay.setProperty("page", page);
            overlay.setProperty("selection", false);
            const auto dimensions = document.pages()[page].toMap();
            for (double zoom : {0.5, 2.0})
            {
                overlay.setWidth(dimensions["width"].toDouble() * zoom);
                overlay.setHeight(dimensions["height"].toDouble() * zoom);
                QImage canvas(QSize(qRound(overlay.width()), qRound(overlay.height())),
                              QImage::Format_ARGB32);
                canvas.fill(Qt::transparent);
                QPainter painter(&canvas);
                overlay.paint(&painter);
                painter.end();
                QCOMPARE(canvas.pixelColor(qRound(20 * zoom), qRound(20 * zoom)).alpha(), 0);
                if (page > 0)
                    QCOMPARE(canvas.pixelColor(qRound(65 * zoom), qRound(85 * zoom)),
                             QColor(Qt::magenta));
                else
                {
                    bool ink = false;
                    for (int y = 80 * zoom; y < 110 * zoom; ++y)
                        for (int x = 60 * zoom; x < 150 * zoom; ++x)
                            ink |= canvas.pixelColor(x, y).alpha() > 0;
                    QVERIFY(ink);
                }
            }
        }
        QVERIFY(!content->geometry(textId, NAN, 0, 10, 10));
        QVERIFY(content->geometry(textId, -20, 900, 900, 900));
        QCOMPARE(content->object(textId)["x"].toDouble(), 0.0);
        QCOMPARE(content->object(textId)["page"].toInt(), 0);
        QCOMPARE(content->object(textId)["text"].toString(), text);
        QVERIFY(content->setText(textId, "Edited text"));
        QCOMPARE(content->object(textId)["text"].toString(), "Edited text");
        for (int id : ids)
        {
            content->select(id);
            content->removeSelected();
            QVERIFY(content->object(id).isEmpty());
        }
        QCOMPARE(content->count(), 0);
        QVERIFY(content->addText(0, 10, 10, "Temporary") > 0);
        document.open(fixture("normal/single-page.pdf"));
        QCOMPARE(content->count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        document.close();
        source.seek(0);
        QCOMPARE(source.readAll(), original);
    }
    void addedContentMouseInteraction()
    {
        PdfDocument document;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("pdfDocument", &document);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        document.open(fixture("normal/multi-page.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        auto* content = document.additions();
        auto click = [&](QString name)
        {
            auto* target = item(window->contentItem(), name);
            if (!target)
                return false;
            QTest::mouseClick(
                window, Qt::LeftButton, Qt::NoModifier,
                target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
            return true;
        };
        QVERIFY(click("addTextButton"));
        auto* dialog = engine.rootObjects().first()->findChild<QObject*>("textDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto* input = item(window->contentItem(), "addedTextInput");
        QVERIFY(input);
        input->setProperty("text", "User entered text");
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_COMPARE(window->property("placement").toString(), "text");
        auto* mouse = item(window->contentItem(), "contentMouse0");
        QVERIFY(mouse);
        auto scene = [&](double x, double y)
        { return mouse->mapToScene(QPointF(x * document.zoom(), y * document.zoom())).toPoint(); };
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, scene(45, 50));
        QTRY_COMPARE(content->count(), 1);
        const int textId = content->selected();
        QCOMPARE(content->object(textId)["text"].toString(), "User entered text");
        QVERIFY(std::abs(content->object(textId)["x"].toDouble() - 45) < 2);
        QTemporaryDir dir;
        QImage image(80, 40, QImage::Format_ARGB32);
        image.fill(Qt::magenta);
        const auto path = dir.filePath("image.png");
        QVERIFY(image.save(path));
        for (const QString type : {QString("text"), QString("image"), QString("signature")})
        {
            if (type != "text")
            {
                auto* picker = engine.rootObjects().first()->findChild<QObject*>("imageDialog");
                QVERIFY(picker);
                picker->setProperty("currentFolder", QUrl::fromLocalFile(dir.path()));
                picker->setProperty("selectedFile", QUrl::fromLocalFile(path));
                QVERIFY(click(type == "signature" ? "addSignatureButton" : "addImageButton"));
                QTRY_VERIFY(picker->property("visible").toBool());
                QTest::qWait(100);
                QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
                QTRY_COMPARE(window->property("placement").toString(), type);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, scene(45, 50));
            }
            const int id = content->selected();
            QCOMPARE(content->object(id)["type"].toString(), type);
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scene(60, 60));
            QTest::mouseMove(window, scene(75, 80), 20);
            QTest::mouseMove(window, scene(90, 100), 20);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, scene(90, 100));
            QTRY_VERIFY(content->object(id)["x"].toDouble() > 65);
            const auto before = content->object(id);
            const double right =
                before["x"].toDouble() + before["width"].toDouble() - 2 / document.zoom();
            const double bottom =
                before["y"].toDouble() + before["height"].toDouble() - 2 / document.zoom();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scene(right, bottom));
            QTest::mouseMove(window, scene(right + 25, bottom + 20), 20);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
                                scene(right + 25, bottom + 20));
            QTRY_VERIFY(content->object(id)["width"].toDouble() > before["width"].toDouble() + 15);
            QVERIFY(click("deleteObjectButton"));
            QTRY_COMPARE(content->count(), 0);
        }
        content->addText(0, 40, 115, "Do not discard silently");
        content->addImage(0, 100, 180, QUrl::fromLocalFile(path));
        content->addImage(0, 300, 280, QUrl::fromLocalFile(path), true);
        if (qEnvironmentVariableIsSet("PDF_CONTENT_SCREENSHOT"))
        {
            QTest::qWait(150);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("PDF_CONTENT_SCREENSHOT")));
        }
        auto* discard = engine.rootObjects().first()->findChild<QObject*>("discardDialog");
        QVERIFY(discard);
        QVERIFY(!window->close());
        QTRY_VERIFY(discard->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(discard, "reject"));
        QVERIFY(window->isVisible());
        QVERIFY(document.ready());
        QVERIFY(click("closeButton"));
        QTRY_VERIFY(discard->property("visible").toBool());
        QVERIFY(document.ready());
        QVERIFY(QMetaObject::invokeMethod(discard, "reject"));
        QVERIFY(document.ready());
        QCOMPARE(content->count(), 3);
        QVERIFY(click("closeButton"));
        QVERIFY(QMetaObject::invokeMethod(discard, "discarded"));
        QTRY_VERIFY(!document.ready());
        QCOMPARE(content->count(), 0);
        window->close();
    }

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
    qmlRegisterType<AddedOverlay>("PdfEditor", 1, 0, "AddedOverlay");
    qmlRegisterUncreatableType<AddedContent>("PdfEditor", 1, 0, "AddedContent",
                                             "Owned by document");
    qmlRegisterType<PdfPageItem>("PdfEditor", 1, 0, "PdfPage");
    qmlRegisterUncreatableType<PdfDocument>("PdfEditor", 1, 0, "PdfDocument",
                                            "Provided by the application");
    ViewerTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_viewer.moc"
