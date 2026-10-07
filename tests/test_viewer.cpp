// SPDX-License-Identifier: GPL-3.0-only
#include "pdf/PdfDocument.h"
#include "ui/AddedOverlay.h"
#include "ui/FormInput.h"
#include "ui/PdfPageItem.h"
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include <unistd.h>

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
  private:
    void checkIndependent(const QString& path, const QString& kind)
    {
#ifdef SAVE_NODE
        QProcess reader;
        const QString script =
            QString(TEST_ROOT) +
            (kind == "xfa" ? "/tools/check_xfa_with_pdfjs.mjs" : "/tools/check_saved_content.mjs");
        reader.start(QString(SAVE_NODE), {script, QString(SAVE_PDFJS), path,
                                          kind == "xfa" ? QString("original") : kind});
        QVERIFY(reader.waitForFinished(20000));
        const QByteArray diagnostics =
            reader.readAllStandardOutput() + reader.readAllStandardError();
        QVERIFY2(reader.exitCode() == 0 && reader.exitStatus() == QProcess::NormalExit,
                 diagnostics.constData());
#else
        Q_UNUSED(path);
        Q_UNUSED(kind);
#endif
    }
    void checkFormOutput(const QString& path, const QString& kind, const QString& checked = "1",
                         const QString& radio = "B", const QString& dropdown = "Beta")
    {
#ifdef SAVE_NODE
        QProcess reader;
        reader.start(QString(SAVE_NODE),
                     {QString(TEST_ROOT) + "/tools/check_form_controls.mjs", QString(SAVE_PDFJS),
                      path, kind, checked, radio, dropdown});
        QVERIFY(reader.waitForFinished(20000));
        const auto diagnostics = reader.readAllStandardOutput() + reader.readAllStandardError();
        QVERIFY2(reader.exitCode() == 0 && reader.exitStatus() == QProcess::NormalExit,
                 diagnostics.constData());
#else
        Q_UNUSED(path);
        Q_UNUSED(kind);
        Q_UNUSED(checked);
        Q_UNUSED(radio);
        Q_UNUSED(dropdown);
#endif
    }
  private slots:
    void arialCaptionUsesMetricCompatibleFont()
    {
        PdfDocument document;
        document.open(fixture("xfa-dynamic/arial-caption.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QSignalSpy images(&document, &PdfDocument::rendered);
        document.requestRender(0, 1224);
        QTRY_VERIFY_WITH_TIMEOUT(!images.isEmpty(), 10000);
        const QImage image = qvariant_cast<QImage>(images.first()[1]);
        // Same rich caption at y=20 and y=60 pt: Arial fallback must match
        // explicit Liberation Sans, including the final word within the box.
        const QImage requested = image.copy(64, 40, 470, 32);
        const QImage reference = image.copy(64, 120, 470, 32);
        QVERIFY(!requested.isNull());
        QCOMPARE(requested, reference);
        bool finalWordVisible = false;
        for (int y = 0; y < requested.height(); ++y)
            for (int x = 420; x < requested.width(); ++x)
                finalWordVisible |= qGray(requested.pixel(x, y)) < 100;
        QVERIFY(finalWordVisible);
    }

    void formEventsAndPersistence_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("acroform") << QString("acroform/controls.pdf");
        QTest::newRow("xfa") << QString("xfa-dynamic/controls.pdf");
        QTest::newRow("xfa-empty-calculation") << QString("xfa-dynamic/empty-calculation.pdf");
    }
    void formEventsAndPersistence()
    {
        QFETCH(QString, path);
        const bool xfa = path.startsWith("xfa");
        PdfDocument document;
        document.open(fixture(path));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        auto draw = [&]()
        {
            QSignalSpy images(&document, &PdfDocument::rendered);
            document.requestRender(0, 612);
            if (!images.wait(10000))
                return QImage{};
            return qvariant_cast<QImage>(images.first()[1]);
        };
        auto event = [&](QString action, double x = 0, double y = 0, int key = 0, QString text = {},
                         int flags = 0)
        {
            QSignalSpy done(&document, &PdfDocument::formEventFinished);
            const quint64 id = document.formEvent(0, action, x, y, key, text, flags);
            if (!id)
                return false;
            const bool completed = done.wait(10000);
            if (!completed || !document.formError().isEmpty())
                return false;
            return done.first()[0].toULongLong() == id && done.first()[1].toBool();
        };
        QVERIFY(!draw().isNull());
        QSignalSpy rejected(&document, &PdfDocument::formEventFinished);
        QVERIFY(document.formEvent(0, "key", 0, 0, 1000));
        QVERIFY(rejected.wait(10000));
        QVERIFY(!rejected.first()[1].toBool());
        QVERIFY(!document.formError().isEmpty());
        QVERIFY(document.ready());
        QVERIFY(!document.formBusy());
        QVERIFY(!document.dirty());
        QVERIFY(event("click", 100, 87));
        QCOMPARE(document.formText(), "original");
        QVERIFY(event("selectAll"));
        QVERIFY(event("text", 0, 0, 0, "edited"));
        QCOMPARE(document.formText(), "edited");
        QVERIFY(event("key", 0, 0, 9)); // Native keyboard focus traversal commits the text.
        if (xfa)
        {
            QCOMPARE(document.formText(), "JS:edited");
        }
        QVERIFY(document.dirty());
        QTemporaryDir states;
        auto saveCheckbox = [&](const QString& checked)
        {
            const auto filename = states.filePath("state-" + checked + ".pdf");
            document.saveAs(QUrl::fromLocalFile(filename));
            QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
            QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
            checkFormOutput(filename, xfa ? "xfa" : "acroform", checked, "A", "Alpha");
        };
        QFile originalFile(fixture(path).toLocalFile());
        QVERIFY(originalFile.open(QIODevice::ReadOnly));
        const QByteArray original = originalFile.readAll();
        QVERIFY(event("click", 82, xfa ? 212 : 150));
        saveCheckbox("1");
        QVERIFY(event("click", 82, xfa ? 212 : 150));
        saveCheckbox("0");
        QVERIFY(event("click", 82, xfa ? 212 : 150));  // leave checked
        QVERIFY(event("click", 152, xfa ? 272 : 212)); // choose B
        QVERIFY(event("click", xfa ? 268 : 262, xfa ? 337 : 275));
        QVERIFY(!draw().isNull());
        QVERIFY(event("key", 0, 0, 40));

        QVERIFY(event("key", 0, 0, 13)); // choose Beta
        QVERIFY(event("blur"));
        QTemporaryDir directory;
        const auto output = QUrl::fromLocalFile(directory.filePath("saved.pdf"));
        if (!xfa)
            document.additions()->addText(0, 50, 400, "Form and additions");
        document.saveAs(QUrl::fromLocalFile(directory.filePath("missing/failure.pdf")));
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY(!document.saveError().isEmpty());
        QVERIFY(document.dirty());
        QVERIFY(document.ready());
        document.saveAs(output);
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
        QVERIFY(!document.dirty());
        QFile source(fixture(path).toLocalFile());
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), original);
        checkFormOutput(output.toLocalFile(), xfa ? "xfa" : "acroform");
        if (qEnvironmentVariableIsSet("PDF_FORM_ARTIFACT_DIR"))
        {
            QDir dir(qEnvironmentVariable("PDF_FORM_ARTIFACT_DIR"));
            QVERIFY(dir.mkpath("."));
            QVERIFY(
                QFile::copy(output.toLocalFile(), dir.filePath(xfa ? "xfa.pdf" : "acroform.pdf")));
        }
        document.close();
        document.open(output);
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QVERIFY(event("click", 100, 87));
        QCOMPARE(document.formText(), "edited");
        if (xfa)
        {
            QVERIFY(event("click", 100, 147));
            QCOMPARE(document.formText(), "JS:edited");
        }
        QVERIFY(event("click", 110, xfa ? 337 : 275));
        QCOMPARE(document.formText(), "Beta");
        document.close();
    }
    void formMouseAndKeyboard_data() { formEventsAndPersistence_data(); }
    void formMouseAndKeyboard()
    {
        QFETCH(QString, path);
        const bool xfa = path.startsWith("xfa");
        PdfDocument document;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("pdfDocument", &document);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        document.open(fixture(path));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QTRY_VERIFY(item(window->contentItem(), "formInput0"));
        auto* input = item(window->contentItem(), "formInput0");
        auto* page = qobject_cast<PdfPageItem*>(item(window->contentItem(), "page0"));
        QVERIFY(page);
        QTRY_VERIFY_WITH_TIMEOUT(page->rendered(), 15000);
        auto click = [&](double x, double y)
        {
            QTest::mouseClick(
                window, Qt::LeftButton, Qt::NoModifier,
                input->mapToScene(QPointF(x / 612 * input->width(), y / 792 * input->height()))
                    .toPoint());
        };
        click(100, 87);
        QTRY_COMPARE(document.formText(), "original");
        QVERIFY(input->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        for (const char character : QByteArray("edited"))
            QTest::keyClick(window, character);
        QTRY_COMPARE(document.formText(), "edited");
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), "edited");
        QTest::keyClick(window, Qt::Key_Delete);
        QTRY_COMPARE(document.formText(), "");
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        QTRY_COMPARE(document.formText(), "edited");
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(!document.formBusy());
        QVERIFY(input->hasActiveFocus());
        if (xfa)
            QCOMPARE(document.formText(), "JS:edited");
        QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
        QTRY_COMPARE(document.formText(), "edited");
        click(82, xfa ? 212 : 150);
        QTRY_VERIFY(!document.formBusy());
        click(152, xfa ? 272 : 212);
        QTRY_VERIFY(!document.formBusy());
        click(xfa ? 268 : 262, xfa ? 337 : 275);
        QTRY_COMPARE(document.formText(), "Alpha");
        QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(document.formText(), "Beta");
        QTRY_VERIFY(!document.formBusy());
        QSignalSpy refreshed(page, &PdfPageItem::renderedChanged);
        document.commitForm();
        QTRY_VERIFY(!document.formBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!refreshed.isEmpty(), 15000);
        QVERIFY(document.dirty());
        if (qEnvironmentVariableIsSet("PDF_FORM_SCREENSHOT") && xfa)
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("PDF_FORM_SCREENSHOT")));
        QTemporaryDir directory;
        const auto output = QUrl::fromLocalFile(directory.filePath("filled.pdf"));
        document.saveAs(output);
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
        checkFormOutput(output.toLocalFile(), xfa ? "xfa" : "acroform");
        document.close();
        QVERIFY(window->close());
    }
    void saveAdditionsAndReopen()
    {
        PdfDocument document;
        document.open(fixture("normal/multi-page.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage image(80, 40, QImage::Format_ARGB32);
        image.fill(Qt::magenta);
        const QString imagePath = dir.filePath("image.png");
        QVERIFY(image.save(imagePath));
        auto* content = document.additions();
        content->addText(0, 50, 150, QString::fromUtf8("Saved résumé <literal>"));
        content->addImage(1, 50, 150, QUrl::fromLocalFile(imagePath));
        QImage signature(80, 40, QImage::Format_ARGB32);
        signature.fill(Qt::transparent);
        {
            QPainter painter(&signature);
            painter.fillRect(QRect(10, 8, 60, 24), Qt::magenta);
        }
        const QString signaturePath = dir.filePath("signature.png");
        QVERIFY(signature.save(signaturePath));
        content->addImage(2, 50, 150, QUrl::fromLocalFile(signaturePath), true);
        QVERIFY(document.dirty());
        QFile source(fixture("normal/multi-page.pdf").toLocalFile());
        QVERIFY(source.open(QIODevice::ReadOnly));
        const auto original = source.readAll();
        document.saveAs(fixture("normal/multi-page.pdf"));
        QVERIFY(!document.saveError().isEmpty());
        QVERIFY(document.dirty());
        document.saveAs(QUrl::fromLocalFile(dir.filePath("missing/output.pdf")));
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY(!document.saveError().isEmpty());
        QCOMPARE(content->count(), 3);
        QVERIFY(document.ready());
        QVERIFY(document.dirty());
        const auto output = QUrl::fromLocalFile(dir.filePath("saved.pdf"));
        document.saveAs(output);
        QVERIFY(document.saving());
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
        QVERIFY(!document.dirty());
        QCOMPARE(content->count(), 3);
        QVERIFY(QFileInfo::exists(output.toLocalFile()));
        // Repeated Save As starts from the snapshot, without duplicating additions.
        const auto second = QUrl::fromLocalFile(dir.filePath("second.pdf"));
        document.saveAs(second);
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY(document.saveError().isEmpty());
        source.seek(0);
        QCOMPARE(source.readAll(), original);
        checkIndependent(second.toLocalFile(), "content");
        if (qEnvironmentVariableIsSet("PDF_SAVE_ARTIFACT_DIR"))
        {
            QDir artifacts(qEnvironmentVariable("PDF_SAVE_ARTIFACT_DIR"));
            QVERIFY(artifacts.mkpath("."));
            QVERIFY(QFile::copy(output.toLocalFile(), artifacts.filePath("saved.pdf")));
        }
        document.close();
        document.open(second);
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QCOMPARE(content->count(), 0);
        for (int page = 0; page < 3; ++page)
        {
            QSignalSpy images(&document, &PdfDocument::rendered);
            document.requestRender(page, 600);
            QTRY_VERIFY_WITH_TIMEOUT(!images.isEmpty(), 10000);
            const auto rendered = qvariant_cast<QImage>(images.first()[1]);
            const auto info = document.pages()[page].toMap();
            const double scale = 600 / info["width"].toDouble();
            if (page > 0)
                QCOMPARE(rendered.pixelColor(qRound(80 * scale), qRound(170 * scale)),
                         QColor(Qt::magenta));
            // Existing panel remains intact outside the added object.
            const QColor expected = page == 0   ? QColor::fromRgbF(.2, .45, .8)
                                    : page == 1 ? QColor::fromRgbF(.2, .65, .4)
                                                : QColor::fromRgbF(.9, .5, .16);
            if (page == 2)
            {
                const auto transparent =
                    rendered.pixelColor(qRound(55 * scale), qRound(155 * scale));
                QVERIFY(std::abs(transparent.red() - expected.red()) <= 1);
                QVERIFY(std::abs(transparent.green() - expected.green()) <= 1);
                QVERIFY(std::abs(transparent.blue() - expected.blue()) <= 1);
            }
            const auto pixel = rendered.pixelColor(rendered.width() / 2, rendered.height() * 3 / 4);
            QVERIFY(std::abs(pixel.red() - expected.red()) <= 1);
            QVERIFY(std::abs(pixel.green() - expected.green()) <= 1);
            QVERIFY(std::abs(pixel.blue() - expected.blue()) <= 1);
        }
        document.close();
    }
    void saveRotatedAndCroppedPages()
    {
        PdfDocument document;
        document.open(fixture("normal/rotated-cropped.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QTemporaryDir dir;
        QImage image(80, 40, QImage::Format_ARGB32);
        image.fill(Qt::magenta);
        const QString path = dir.filePath("image.png");
        QVERIFY(image.save(path));
        for (int page = 0; page < 3; ++page)
            document.additions()->addImage(page, 35, 75, QUrl::fromLocalFile(path));
        const auto dimensions = document.pages();
        const QUrl output = QUrl::fromLocalFile(dir.filePath("rotated.pdf"));
        document.saveAs(output);
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
        document.close();
        document.open(output);
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QCOMPARE(document.pages(), dimensions);
        for (int page = 0; page < 3; ++page)
        {
            QSignalSpy images(&document, &PdfDocument::rendered);
            document.requestRender(page, 600);
            QTRY_VERIFY_WITH_TIMEOUT(!images.isEmpty(), 10000);
            const QImage rendered = qvariant_cast<QImage>(images.first()[1]);
            const double scale = 600 / dimensions[page].toMap()["width"].toDouble();
            QCOMPARE(rendered.pixelColor(qRound(50 * scale), qRound(90 * scale)),
                     QColor(Qt::magenta));
            QCOMPARE(rendered.pixelColor(qRound(20 * scale), qRound(20 * scale)),
                     QColor(Qt::white));
        }
        document.close();
    }
    void saveSnapshotFailureAndConcurrentEdits()
    {
        QTemporaryDir dir;
        const QString input = dir.filePath("input.pdf");
        QVERIFY(QFile::copy(fixture("normal/single-page.pdf").toLocalFile(), input));
        PdfDocument document;
        document.open(QUrl::fromLocalFile(input));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        const QString alias = dir.filePath("alias.pdf"), hardlink = dir.filePath("hardlink.pdf");
        QVERIFY(QFile::link(input, alias));
        QCOMPARE(::link(input.toLocal8Bit().constData(), hardlink.toLocal8Bit().constData()), 0);
        for (const QString& target : {alias, hardlink})
        {
            document.saveAs(QUrl::fromLocalFile(target));
            QVERIFY(!document.saving());
            QVERIFY(!document.saveError().isEmpty());
        }
        document.additions()->addText(0, 40, 120, "snapshot text");
        // Replacement on disk must not change the open document's save baseline.
        QFile changed(input);
        QVERIFY(changed.open(QIODevice::WriteOnly | QIODevice::Truncate));
        changed.write("externally replaced");
        changed.close();
        document.saveAs(QUrl::fromLocalFile(dir.filePath("copy.pdf")));
        document.additions()->addText(0, 40, 220, "later edit");
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY(document.saveError().isEmpty());
        QVERIFY(document.dirty());
        QCOMPARE(document.additions()->count(), 2);
        const auto pathBefore = document.savedPath();
        const QByteArray previousPath = qgetenv("PATH");
        qputenv("PATH", "/nonexistent");
        document.saveAs(QUrl::fromLocalFile(dir.filePath("failed.pdf")));
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        qputenv("PATH", previousPath);
        QVERIFY(!document.saveError().isEmpty());
        QVERIFY(document.ready());
        QCOMPARE(document.additions()->count(), 2);
        QCOMPARE(document.savedPath(), pathBefore);
        document.close();
        document.open(QUrl::fromLocalFile(dir.filePath("copy.pdf")));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
    }
    void saveExistingForms()
    {
        QTemporaryDir dir;
        for (const auto& name :
             {QString("xfa-javascript/calculation.pdf"), QString("acroform/text.pdf")})
        {
            PdfDocument document;
            document.open(fixture(name));
            QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
            if (name.startsWith("acroform"))
                document.additions()->addText(0, 40, 220, "Added beside form");
            const auto output = QUrl::fromLocalFile(
                dir.filePath(name.startsWith("acroform") ? "acroform.pdf" : "xfa.pdf"));
            document.saveAs(output);
            QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
            QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
            checkIndependent(output.toLocalFile(), name.startsWith("xfa") ? "xfa" : "acroform");
#ifdef SAVE_NODE
            if (name.startsWith("xfa"))
            {
                QProcess probe;
                probe.start(QString(SAVE_PYTHON),
                            {QString(TEST_ROOT) + "/tools/run_xfa_probe.py", "--worker",
                             QCoreApplication::applicationDirPath() + "/xfa-probe-worker",
                             "--pdfium", QString(SAVE_PDFIUM), "--input", output.toLocalFile(),
                             "--output", dir.filePath("native-roundtrip")});
                QVERIFY(probe.waitForFinished(30000));
                const QByteArray diagnostics =
                    probe.readAllStandardOutput() + probe.readAllStandardError();
                QVERIFY2(probe.exitCode() == 0 && probe.exitStatus() == QProcess::NormalExit,
                         diagnostics.constData());
            }
#endif
            const auto type = document.formType();
            document.close();
            document.open(output);
            QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
            QCOMPARE(document.formType(), type);
            QSignalSpy images(&document, &PdfDocument::rendered);
            document.requestRender(0, 612);
            QTRY_VERIFY_WITH_TIMEOUT(!images.isEmpty(), 10000);
            if (name.startsWith("acroform"))
            {
                auto event = [&](const QString& action, const QString& text = {})
                {
                    QSignalSpy done(&document, &PdfDocument::formEventFinished);
                    document.formEvent(0, action, 100, 152, 0, text);
                    return done.wait(10000) && done.first()[1].toBool();
                };
                QVERIFY(event("click"));
                QCOMPARE(document.formText(), "Edited value");
                QVERIFY(event("selectAll"));
                QVERIFY(event("text", "ABC"));
                QCOMPARE(document.formText(), "ABC");
            }
            if (name.startsWith("xfa"))
            {
                document.additions()->addText(0, 20, 400, "retained");
                document.saveAs(QUrl::fromLocalFile(dir.filePath("unsupported.pdf")));
                QVERIFY(!document.saving());
                QVERIFY(!document.saveError().isEmpty());
                QCOMPARE(document.additions()->count(), 1);
                QVERIFY(document.ready());
            }
            if (qEnvironmentVariableIsSet("PDF_SAVE_ARTIFACT_DIR"))
            {
                QDir artifacts(qEnvironmentVariable("PDF_SAVE_ARTIFACT_DIR"));
                QVERIFY(
                    QFile::copy(output.toLocalFile(),
                                artifacts.filePath(QFileInfo(output.toLocalFile()).fileName())));
            }
            document.close();
        }
    }

    void saveDialogInteraction()
    {
        PdfDocument document;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("pdfDocument", &document);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        document.open(fixture("normal/single-page.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        document.additions()->addText(0, 50, 150, "Saved through the dialog");
        QTemporaryDir dir;
        auto* dialog = engine.rootObjects().first()->findChild<QObject*>("saveDialog");
        QVERIFY(dialog);
        dialog->setProperty("currentFolder", QUrl::fromLocalFile(dir.path()));
        dialog->setProperty("selectedFile", QUrl::fromLocalFile(dir.filePath("saved.pdf")));
        auto* button = item(window->contentItem(), "saveButton");
        QVERIFY(button);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene({button->width() / 2, button->height() / 2}).toPoint());
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY_WITH_TIMEOUT(!document.savedPath().isEmpty() || !document.saveError().isEmpty(),
                                 30000);
        QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
        QVERIFY(!document.dirty());
        QVERIFY(QFileInfo::exists(document.savedPath()));
        QVERIFY(window->close());
        document.close();
    }
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
    void fileDialogsStartInConfiguredFolder()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PdfDocument document;
        QQmlApplicationEngine engine;
        const QUrl folder = QUrl::fromLocalFile(directory.path());
        engine.rootContext()->setContextProperty("pdfDocument", &document);
        engine.rootContext()->setContextProperty("startupFolder", folder);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = engine.rootObjects().first();
        for (const QString& name :
             {QString("openDialog"), QString("saveDialog"), QString("imageDialog")})
        {
            auto* dialog = window->findChild<QObject*>(name);
            QVERIFY(dialog);
            QCOMPARE(dialog->property("currentFolder").toUrl(), folder);
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
    qmlRegisterType<FormInput>("PdfEditor", 1, 0, "FormInput");
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
