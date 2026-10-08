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
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
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
    void xfaCrossPageNavigation()
    {
        PdfDocument document;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("pdfDocument", &document);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        document.open(fixture("xfa-dynamic/phase3-navigation.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QCOMPARE(document.pageCount(), 2);
        auto* surface = item(window->contentItem(), "formInput0");
        QVERIFY(surface);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            surface->mapToScene(QPointF(100 * document.zoom(), 87 * document.zoom())).toPoint());
        QTRY_COMPARE(document.formText(), QString("first"));
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_COMPARE(document.currentPage(), 2);
        QTRY_COMPARE(document.formText(), QString("second"));
        QTRY_VERIFY(window->activeFocusItem() &&
                    window->activeFocusItem()->objectName() == "formInput1");
        surface = item(window->contentItem(), "formInput1");
        const auto point =
            surface->mapToScene(QPointF(100 * document.zoom(), 615 * document.zoom()));
        QVERIFY(point.y() > 210 && point.y() < window->height() - 60);
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Z);
        QTRY_COMPARE(document.formText(), QString("z"));
        QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
        QTRY_COMPARE(document.currentPage(), 1);
        QTRY_COMPARE(document.formText(), QString("first"));
        document.resetForm();
        QTRY_VERIFY(!document.formBusy());
        QVERIFY2(document.formError().isEmpty(), qPrintable(document.formError()));
        QTemporaryDir directory;
        for (int generation = 1; generation <= 2; ++generation)
        {
            const auto output =
                directory.filePath(QString("xfa-navigation-%1.pdf").arg(generation));
            document.saveAs(QUrl::fromLocalFile(output));
            QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
            QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
#ifdef SAVE_NODE
            QProcess reader;
            reader.start(QString(SAVE_NODE), {QString(TEST_ROOT) + "/tools/check_phase3_forms.mjs",
                                              QString(SAVE_PDFJS), output, "xfa-navigation"});
            QVERIFY(reader.waitForFinished(20000));
            const auto diagnostics = reader.readAllStandardOutput() + reader.readAllStandardError();
            QVERIFY2(reader.exitCode() == 0, diagnostics.constData());
#endif
        }
        document.close();
    }

    void formExperienceUi()
    {
        PdfDocument document;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("pdfDocument", &document);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        document.open(fixture("acroform/phase3.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        auto click = [&](const QString& name)
        {
            auto* target = item(window->contentItem(), name);
            if (!target)
                return false;
            QTest::mouseClick(
                window, Qt::LeftButton, Qt::NoModifier,
                target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
            return true;
        };
        const auto dialog = [&](const QString& name) { return window->findChild<QObject*>(name); };
        QVERIFY(click("validateFormButton"));
        QTRY_VERIFY(dialog("formValidationDialog")->property("visible").toBool());
        QCOMPARE(document.formValidation().size(), 3);
        QVERIFY(click("formIssueItem0"));
        QTRY_COMPARE(document.focusedField()["id"].toInt(), 0);
        document.setFormFieldText(0, "Alice");
        QTRY_VERIFY(!document.formBusy());
        QVERIFY(click("nextFieldButton"));
        QTRY_COMPARE(document.focusedField()["id"].toInt(), 1);
        QVERIFY(click("enterFormDateButton"));
        QTRY_VERIFY(dialog("formDateDialog")->property("visible").toBool());
        item(window->contentItem(), "dateYear")->setProperty("value", 2028);
        item(window->contentItem(), "dateMonth")->setProperty("value", 2);
        item(window->contentItem(), "dateDay")->setProperty("value", 29);
        QVERIFY(QMetaObject::invokeMethod(dialog("formDateDialog"), "accept"));
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(document.formFields()[1].toMap()["value"].toString(), QString("2028-02-29"));
        QVERIFY(click("formFieldsButton"));
        QTRY_VERIFY(dialog("fieldsDialog")->property("visible").toBool());
        QVERIFY(click("formFieldItem2"));
        QTRY_COMPARE(document.focusedField()["id"].toInt(), 2);
        QVERIFY(click("chooseFormValueButton"));
        QTRY_VERIFY(dialog("formChoiceDialog")->property("visible").toBool());
        QVERIFY(click("formOptionItem1"));
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(document.formFields()[2].toMap()["value"].toString(), QString("Beta"));
        QVERIFY(click("formFieldsButton"));
        QTRY_VERIFY(dialog("fieldsDialog")->property("visible").toBool());
        QVERIFY(click("formFieldItem6"));
        QTRY_COMPARE(document.currentPage(), 2);
        QTRY_COMPARE(document.focusedField()["id"].toInt(), 6);
        QTRY_VERIFY(window->activeFocusItem() &&
                    window->activeFocusItem()->objectName() == "formInput1");
        auto* surface = item(window->contentItem(), "formInput1");
        const auto fieldPoint =
            surface->mapToScene(QPointF(100 * document.zoom(), 815 * document.zoom()));
        QVERIFY(fieldPoint.y() > 210 && fieldPoint.y() < window->height() - 60);
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_X);
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(document.formFields()[6].toMap()["value"].toString(), QString("x"));
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_X, Qt::ControlModifier);
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("x"));
        QCOMPARE(document.formText(), QString());
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        QTRY_COMPARE(document.formText(), QString("x"));
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_COMPARE(document.currentPage(), 1);
        QTRY_VERIFY(window->activeFocusItem() &&
                    window->activeFocusItem()->objectName() == "formInput0");
        if (qEnvironmentVariableIsSet("PDF_FORM_EXPERIENCE_SCREENSHOT"))
        {
            QTest::qWait(150);
            QVERIFY(
                window->grabWindow().save(qEnvironmentVariable("PDF_FORM_EXPERIENCE_SCREENSHOT")));
        }
        QVERIFY(click("resetWholeFormButton"));
        QTRY_VERIFY(dialog("resetFormDialog")->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog("resetFormDialog"), "reject"));
        QCOMPARE(document.formFields()[0].toMap()["value"].toString(), QString("Alice"));
        document.additions()->addText(0, 400, 400, "Keep me");
        QVERIFY(click("resetWholeFormButton"));
        QTRY_VERIFY(dialog("resetFormDialog")->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog("resetFormDialog"), "accept"));
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(document.formFields()[0].toMap()["value"].toString(), QString());
        QCOMPARE(document.additions()->count(), 1);
        QVERIFY(click("signFormButton"));
        QTRY_VERIFY(dialog("signDialog")->property("visible").toBool());
        QVERIFY(click("drawSignatureButton"));
        QTRY_VERIFY(dialog("graphicDialog")->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog("graphicDialog"), "accept"));
        QTRY_COMPARE(window->property("placement").toString(), QString("drawing"));
        auto* mouse = item(window->contentItem(), "contentMouse0");
        const auto point = [&](double x, double y)
        { return mouse->mapToScene(QPointF(x * document.zoom(), y * document.zoom())).toPoint(); };
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point(90, 410));
        QTest::mouseMove(window, point(150, 420), 20);
        QTest::mouseMove(window, point(200, 410), 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point(200, 410));
        QCOMPARE(document.additions()->count(), 2);
        QCOMPARE(document.additions()->object(document.additions()->selected())["type"].toString(),
                 QString("drawing"));
        document.close();
    }

    void formExperienceHelpers()
    {
        PdfDocument document;
        document.open(fixture("acroform/phase3.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QVERIFY(document.formHelpersAvailable());
        QCOMPARE(document.formFields().size(), 7);
        const auto field = [&](int id) { return document.formFields()[id].toMap(); };
        QCOMPARE(field(1)["dateFormat"].toString(), QString("yyyy-mm-dd"));
        QVERIFY(field(0)["required"].toBool());
        QVERIFY(field(4)["readOnly"].toBool());
        QVERIFY(document.validateForm());
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(document.formValidation().size(), 3);
        QVERIFY(document.setFormFieldText(0, "Alice"));
        QTRY_VERIFY(!document.formBusy());
        QVERIFY2(document.formError().isEmpty(), qPrintable(document.formError()));
        QCOMPARE(field(0)["value"].toString(), QString("Alice"));
        QVERIFY(document.setFormFieldText(1, "2026-02-30"));
        QTRY_VERIFY(!document.formBusy());
        QVERIFY(!document.formError().isEmpty() || !document.formValidation().isEmpty());
        QVERIFY(document.setFormFieldText(1, "2026-10-08"));
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(field(1)["value"].toString(), QString("2026-10-08"));
        QVERIFY(document.chooseFormOption(2, 1));
        QTRY_VERIFY(!document.formBusy());
        QVERIFY2(document.formError().isEmpty(), qPrintable(document.formError()));
        QCOMPARE(field(2)["value"].toString(), QString("Beta"));
        QCOMPARE(document.formValidation().size(), 1);
        document.formEvent(0, "click", 82, 272);
        QTRY_VERIFY(!document.formBusy());
        QVERIFY(field(3)["checked"].toBool());
        QVERIFY(document.formValidation().isEmpty());
        QVERIFY(document.focusFormField(5));
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(document.focusedField()["id"].toInt(), 5);
        QVERIFY(document.navigateForm());
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(document.currentPage(), 2);
        QCOMPARE(document.focusedField()["id"].toInt(), 6);
        QVERIFY(document.navigateForm(true));
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(document.currentPage(), 1);
        QCOMPARE(document.focusedField()["id"].toInt(), 5);
        QVERIFY(document.setFormFieldText(4, "cannot edit"));
        QTRY_VERIFY(!document.formBusy());
        QVERIFY(!document.formError().isEmpty());
        QCOMPARE(field(4)["value"].toString(), QString("LOCKED"));
        QVERIFY(document.resetFormField(0));
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(field(0)["value"].toString(), QString());
        QCOMPARE(field(2)["value"].toString(), QString("Beta"));
        QVERIFY(document.resetForm());
        QTRY_VERIFY(!document.formBusy());
        QVERIFY2(document.formError().isEmpty(), qPrintable(document.formError()));
        QCOMPARE(field(1)["value"].toString(), QString());
        QCOMPARE(field(2)["value"].toString(), QString("Alpha"));
        QVERIFY(!field(3)["checked"].toBool());
        QCOMPARE(document.formValidation().size(), 3);
        QVERIFY(document.setFormFieldText(0, "Saved helper"));
        QVERIFY(document.setFormFieldText(1, "2026-10-08"));
        QVERIFY(document.chooseFormOption(2, 1));
        document.formEvent(0, "click", 82, 272);
        QVERIFY(document.setFormFieldText(6, "Saved second"));
        QTRY_VERIFY(!document.formBusy());
        auto* additions = document.additions();
        additions->beginEdit();
        const int stroke = additions->addGraphic(0, 90, 410, "drawing", "black", 2);
        QVERIFY(additions->appendStroke(stroke, 150, 400));
        QVERIFY(additions->appendStroke(stroke, 200, 415));
        additions->endEdit();
        QTemporaryDir directory;
        QFile source(fixture("acroform/phase3.pdf").toLocalFile());
        QVERIFY(source.open(QIODevice::ReadOnly));
        const auto original = source.readAll();
        for (int generation = 1; generation <= 2; ++generation)
        {
            const auto output =
                directory.filePath(QString("form-experience-%1.pdf").arg(generation));
            document.saveAs(QUrl::fromLocalFile(output));
            QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
            QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
#ifdef SAVE_NODE
            QProcess reader;
            reader.start(QString(SAVE_NODE), {QString(TEST_ROOT) + "/tools/check_phase3_forms.mjs",
                                              QString(SAVE_PDFJS), output, "acroform"});
            QVERIFY(reader.waitForFinished(20000));
            const auto diagnostics = reader.readAllStandardOutput() + reader.readAllStandardError();
            QVERIFY2(reader.exitCode() == 0, diagnostics.constData());
#endif
        }
        source.seek(0);
        QCOMPARE(source.readAll(), original);
        // XFA stays native: helpers fail explicitly and preserve the document.
        document.open(fixture("xfa-dynamic/controls.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QVERIFY(!document.formHelpersAvailable());
        document.formEvent(0, "click", 100, 87);
        document.formEvent(0, "selectAll");
        document.formEvent(0, "text", 0, 0, 0, "changed XFA");
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(document.formText(), QString("changed XFA"));
        document.formEvent(0, "selectAll");
        document.formEvent(0, "cut");
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("changed XFA"));
        QCOMPARE(document.formText(), QString());
        document.additions()->addText(0, 400, 400, "Retained addition");
        QVERIFY(document.canResetForm());
        QVERIFY(document.resetForm());
        QTRY_VERIFY(!document.formBusy());
        QVERIFY2(document.formError().isEmpty(), qPrintable(document.formError()));
        QVERIFY(document.ready());
        QCOMPARE(document.additions()->count(), 1);
        document.formEvent(0, "click", 100, 87);
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(document.formText(), QString("original"));
        document.formEvent(0, "selectAll");
        document.formEvent(0, "text", 0, 0, 0, "edited");
        document.formEvent(0, "blur");
        document.formEvent(0, "click", 100, 147);
        QTRY_VERIFY(!document.formBusy());
        QCOMPARE(document.formText(), QString("JS:edited"));
        document.additions()->removeSelected();
        const auto xfaOutput = directory.filePath("xfa-reset-script.pdf");
        document.saveAs(QUrl::fromLocalFile(xfaOutput));
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
        checkFormOutput(xfaOutput, "xfa", "0", "A", "Alpha");
        document.open(fixture("acroform/controls.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        document.formEvent(0, "click", 152, 212);
        QTRY_VERIFY(!document.formBusy());
        QVERIFY(document.formFields()[3].toMap()["checked"].toBool());
        QVERIFY(document.resetFormField(3));
        QTRY_VERIFY(!document.formBusy());
        QVERIFY2(document.formError().isEmpty(), qPrintable(document.formError()));
        QVERIFY(document.formFields()[2].toMap()["checked"].toBool());
        QVERIFY(!document.formFields()[3].toMap()["checked"].toBool());
        document.open(fixture("acroform/phase3-lock-on-focus.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QVERIFY(document.setFormFieldText(0, "must stay empty"));
        QTRY_VERIFY(!document.formBusy());
        QVERIFY(!document.formError().isEmpty());
        QVERIFY(document.formFields()[0].toMap()["readOnly"].toBool());
        QCOMPARE(document.formFields()[0].toMap()["value"].toString(), QString());
        QVERIFY(document.ready());
    }

    void everydayEditingMouseAndKeyboard()
    {
        PdfDocument document;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("pdfDocument", &document);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        document.open(fixture("normal/blank.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        auto* content = document.additions();
        auto click = [&](const QString& name)
        {
            auto* target = item(window->contentItem(), name);
            if (!target)
                return false;
            QTest::mouseClick(
                window, Qt::LeftButton, Qt::NoModifier,
                target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
            return true;
        };
        auto* mouse = item(window->contentItem(), "contentMouse0");
        QVERIFY(mouse);
        auto scene = [&](double x, double y)
        { return mouse->mapToScene(QPointF(x * document.zoom(), y * document.zoom())).toPoint(); };
        auto* dialog = window->findChild<QObject*>("graphicDialog");
        QVERIFY(dialog);
        int row = 0;
        for (const QString kind :
             {"checkmark", "rectangle", "ellipse", "highlight", "line", "drawing", "stamp"})
        {
            QVERIFY(click("drawToolsButton"));
            QTest::qWait(30);
            QVERIFY(click(kind + "Tool"));
            QTRY_VERIFY(dialog->property("visible").toBool());
            if (kind == "stamp")
            {
                auto* text = item(window->contentItem(), "stampText");
                QVERIFY(text);
                text->setProperty("text", "REVIEWED");
            }
            QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
            QTRY_COMPARE(window->property("placement").toString(), kind);
            const double x = row % 2 ? 280 : 40;
            const double y = 40 + (row / 2) * 110;
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scene(x, y));
            if (kind != "checkmark" && kind != "stamp")
            {
                QTest::mouseMove(window, scene(x + 30, y + 20), 20);
                QTest::mouseMove(window, scene(x + 100, y + 50), 20);
            }
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, scene(x + 100, y + 50));
            ++row;
            QTRY_COMPARE(content->count(), row);
            QCOMPARE(content->object(content->selected())["type"].toString(), kind);
            QVERIFY(!content->editing());
            if (kind == "drawing")
                QVERIFY(content->object(content->selected())["pointCount"].toInt() >= 3);
        }
        if (qEnvironmentVariableIsSet("PDF_PHASE2_SCREENSHOT"))
        {
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("PDF_PHASE2_SCREENSHOT")));
        }
        QVERIFY(click("selectAreaButton"));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scene(30, 30));
        QTest::mouseMove(window, scene(400, 100), 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, scene(400, 100));
        QTRY_COMPARE(content->selectionCount(), 2);
        const auto firstId = content->objects()[0].id, secondId = content->objects()[1].id;
        const auto first = content->object(firstId), second = content->object(secondId);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scene(55, 55));
        QTest::mouseMove(window, scene(65, 65), 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, scene(65, 65));
        QTRY_VERIFY(content->object(firstId)["x"].toDouble() > first["x"].toDouble());
        QCOMPARE(content->object(secondId)["x"].toDouble() - second["x"].toDouble(),
                 content->object(firstId)["x"].toDouble() - first["x"].toDouble());
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(content->object(firstId), first);
        QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        QTRY_COMPARE(content->count(), 9);
        QCOMPARE(content->selectionCount(), 2);
        QTest::keyClick(window, Qt::Key_Delete);
        QTRY_COMPARE(content->count(), 7);
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(content->count(), 9);
        content->undo(); // Undo paste too.
        QCOMPARE(content->count(), 7);
        content->select(secondId);
        const auto box = content->object(secondId);
        for (const QPointF handle : {QPointF(0, 0), QPointF(.5, 0), QPointF(1, 0), QPointF(1, .5),
                                     QPointF(1, 1), QPointF(.5, 1), QPointF(0, 1), QPointF(0, .5)})
        {
            const double x = box["x"].toDouble() + handle.x() * box["width"].toDouble();
            const double y = box["y"].toDouble() + handle.y() * box["height"].toDouble();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scene(x, y));
            QTest::mouseMove(window, scene(x + 10, y + 10), 20);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, scene(x + 10, y + 10));
            QVERIFY(content->object(secondId) != box);
            content->undo();
            QCOMPARE(content->object(secondId), box);
        }
        const double right = box["x"].toDouble() + box["width"].toDouble();
        const double bottom = box["y"].toDouble() + box["height"].toDouble();
        QTest::mousePress(window, Qt::LeftButton, Qt::ShiftModifier, scene(right, bottom));
        // QTest::mouseMove always sends NoModifier; preserve Shift on this event.
        QTest::mouseEvent(QTest::MouseMove, window, Qt::NoButton, Qt::ShiftModifier,
                          scene(right + 20, bottom + 5), 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::ShiftModifier,
                            scene(right + 20, bottom + 5));
        const auto proportional = content->object(secondId);
        QVERIFY(std::abs(proportional["width"].toDouble() / proportional["height"].toDouble() -
                         box["width"].toDouble() / box["height"].toDouble()) < .001);
        QVERIFY(proportional != box);
        content->undo();
        // Shift-click toggles members without moving the group.
        QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, scene(55, 55));
        QCOMPARE(content->selectionCount(), 2);
        QVERIFY(click("alignObjectsButton"));
        QTest::qWait(30);
        QVERIFY(click("alignLeft"));
        QCOMPARE(content->object(firstId)["x"], content->object(secondId)["x"]);
        content->undo();
        content->select(secondId);
        QVERIFY(click("appearanceButton"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        item(window->contentItem(), "inkColor")->setProperty("currentIndex", 2);
        item(window->contentItem(), "inkWidth")->setProperty("value", 5);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QCOMPARE(content->object(secondId)["color"].toString(), QString("#ff225a91"));
        QCOMPARE(content->object(secondId)["lineWidth"].toDouble(), 5.);
        content->undo();
        QCOMPARE(content->object(secondId), box);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, scene(300, 60));
        QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(content->object(secondId)["x"].toDouble(), box["x"].toDouble() + 10);
        content->undo();
        // Cancel a live gesture without discarding the previous redo branch.
        QVERIFY(content->canRedo());
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scene(300, 60));
        QTest::mouseMove(window, scene(310, 70), 20);
        QVERIFY(content->editing());
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::mouseMove(window, scene(320, 80), 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, scene(320, 80));
        QVERIFY(!content->editing());
        QCOMPARE(content->object(secondId), box);
        QVERIFY(content->canRedo());
        QGuiApplication::clipboard()->clear();
        document.close();
    }

    void multiSelectionClipboardAndAlignment()
    {
        PdfDocument document;
        document.open(fixture("normal/multi-page.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        auto* content = document.additions();
        const int first = content->addText(0, 20, 30, "First");
        const int second = content->addText(0, 300, 150, "Second");
        QVERIFY(content->geometry(second, 300, 150, 120, 40));
        QVERIFY(content->setTextStyle(second, "Serif", 28, true, true, true));
        content->select(first);
        content->toggleSelection(second);
        QCOMPARE(content->selectionCount(), 2);
        content->beginEdit();
        QVERIFY(content->moveSelection(10, 10));
        QVERIFY(content->moveSelection(30, 40));
        content->endEdit();
        QCOMPARE(content->object(first)["x"].toDouble(), 50.);
        QCOMPARE(content->object(second)["x"].toDouble(), 330.);
        content->undo();
        QCOMPARE(content->selectionCount(), 2);
        QCOMPARE(content->object(first)["x"].toDouble(), 20.);
        content->redo();
        QVERIFY(content->nudgeSelection(10000, 10000));
        const auto page = document.pages()[0].toMap();
        QCOMPARE(content->object(second)["x"].toDouble() +
                     content->object(second)["width"].toDouble(),
                 page["width"].toDouble());
        QCOMPARE(content->object(second)["y"].toDouble() +
                     content->object(second)["height"].toDouble(),
                 page["height"].toDouble());
        QCOMPARE(content->object(second)["x"].toDouble() - content->object(first)["x"].toDouble(),
                 280.);
        for (const QString mode : {"left", "center", "right", "top", "middle", "bottom"})
        {
            const auto a = content->object(first), b = content->object(second);
            QVERIFY(content->alignSelection(mode));
            const auto coordinate = [&mode](const QVariantMap& object)
            {
                const bool horizontal = mode == "left" || mode == "center" || mode == "right";
                const double position = object[horizontal ? "x" : "y"].toDouble();
                const double size = object[horizontal ? "width" : "height"].toDouble();
                return position + ((mode == "center" || mode == "middle")  ? size / 2
                                   : (mode == "right" || mode == "bottom") ? size
                                                                           : 0);
            };
            QCOMPARE(coordinate(content->object(first)), coordinate(content->object(second)));
            content->undo();
            QCOMPARE(content->object(first), a);
            QCOMPARE(content->object(second), b);
        }
        QVERIFY(content->copySelection());
        const auto payload = QGuiApplication::clipboard()->mimeData()->data(
            "application/vnd.pdf-editor.additions+json");
        QVERIFY(content->paste(1));
        QCOMPARE(content->count(), 4);
        QCOMPARE(content->selectionCount(), 2);
        const int pasted = content->selected();
        QVERIFY(pasted != second);
        QCOMPARE(content->object(pasted)["page"].toInt(), 1);
        QCOMPARE(content->object(pasted)["fontSize"].toInt(), 28);
        QVERIFY(content->object(pasted)["italic"].toBool());
        content->undo();
        QCOMPARE(content->count(), 2);
        content->redo();
        QCOMPARE(content->count(), 4);
        QVERIFY(content->cutSelection());
        QCOMPARE(content->count(), 2);
        content->undo();
        QCOMPARE(content->count(), 4);
        QCOMPARE(content->selectionCount(), 2);
        content->toggleSelection(first);
        QCOMPARE(content->selectionCount(), 1);
        QCOMPARE(content->selected(), first); // Different pages never share a selection.
        const auto revision = content->revision();
        auto parsed = QJsonDocument::fromJson(payload).object();
        auto objects = parsed["objects"].toArray();
        auto invalid = objects[0].toObject();
        invalid["width"] = 1e100;
        objects[0] = invalid;
        parsed["objects"] = objects;
        auto* mime = new QMimeData;
        mime->setData("application/vnd.pdf-editor.additions+json", QJsonDocument(parsed).toJson());
        QGuiApplication::clipboard()->setMimeData(mime);
        QVERIFY(!content->paste(0));
        QCOMPARE(content->revision(), revision);
        QCOMPARE(content->count(), 4);
        QGuiApplication::clipboard()->setText("External clipboard text");
        QVERIFY(content->paste(0));
        QCOMPARE(content->object(content->selected())["text"].toString(),
                 QString("External clipboard text"));
        QImage image(20, 20, QImage::Format_ARGB32);
        image.fill(Qt::magenta);
        QGuiApplication::clipboard()->setImage(image);
        QVERIFY(content->paste(0));
        QCOMPARE(content->objects().last().image, image);
        content->selectAll(1);
        QCOMPARE(content->selectionCount(), 2);
        content->removeSelected();
        QCOMPARE(content->count(), 4);
        content->undo();
        QCOMPARE(content->count(), 6);
        QCOMPARE(content->selectionCount(), 2);
        QGuiApplication::clipboard()->clear();
    }

    void graphicsSaveAndReopen()
    {
        PdfDocument document;
        document.open(fixture("normal/blank.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        auto* content = document.additions();
        QVERIFY(content->addGraphic(0, 40, 40, "checkmark", "#237a38", 3) > 0);
        QVERIFY(content->addGraphic(0, 120, 40, "rectangle", "#225a91", 2) > 0);
        QVERIFY(content->addGraphic(0, 280, 40, "ellipse", "#c62828", 3) > 0);
        const int line = content->addGraphic(0, 40, 150, "line", "black", 2);
        QVERIFY(content->setLine(line, 40, 150, 180, 200));
        QVERIFY(content->addGraphic(0, 260, 160, "highlight", "yellow", 2) > 0);
        QVERIFY(content->addGraphic(0, 40, 260, "stamp", "#c62828", 2, "APPROVED") > 0);
        content->beginEdit();
        const int drawing = content->addGraphic(0, 260, 260, "drawing", "black", 3);
        QVERIFY(content->appendStroke(drawing, 300, 290));
        QVERIFY(content->appendStroke(drawing, 360, 270));
        QVERIFY(content->appendStroke(drawing, 390, 320));
        content->endEdit();
        QCOMPARE(content->object(drawing)["pointCount"].toInt(), 4);
        content->undo();
        QVERIFY(content->object(drawing).isEmpty());
        content->redo();
        QCOMPARE(content->object(drawing)["pointCount"].toInt(), 4);
        const auto revision = content->revision();
        QVERIFY(!content->appendStroke(drawing, NAN, 1));
        QCOMPARE(content->addGraphic(0, 1, 1, "invalid", "black", 2), -1);
        QCOMPARE(content->revision(), revision);
        content->addText(0, 40, 390, "Everyday editing");
        QTemporaryDir directory;
        QImage image(40, 20, QImage::Format_ARGB32);
        image.fill(Qt::magenta);
        const auto path = directory.filePath("image.png");
        QVERIFY(image.save(path));
        content->addImage(0, 260, 390, QUrl::fromLocalFile(path));
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.fillRect(QRect(10, 5, 20, 10), Qt::magenta);
        }
        QVERIFY(image.save(path));
        content->addImage(0, 40, 500, QUrl::fromLocalFile(path), true);
        QCOMPARE(content->count(), 10);
        content->selectAll(0);
        QVERIFY(content->copySelection());
        QVERIFY(content->paste(0));
        QCOMPARE(content->count(), 20);
        for (int i = 0; i < 10; ++i)
        {
            const auto& originalObject = content->objects()[i];
            const auto& pastedObject = content->objects()[i + 10];
            QCOMPARE(originalObject.type, pastedObject.type);
            QCOMPARE(originalObject.points, pastedObject.points);
            QCOMPARE(originalObject.color, pastedObject.color);
            QCOMPARE(originalObject.image, pastedObject.image);
        }
        content->undo();
        QCOMPARE(content->count(), 10);
        QGuiApplication::clipboard()->clear();
        content->beginEdit();
        document.saveAs(QUrl::fromLocalFile(directory.filePath("partial.pdf")));
        QVERIFY(!document.saveError().isEmpty());
        QVERIFY(!QFile::exists(directory.filePath("partial.pdf")));
        content->cancelEdit();
        QFile source(fixture("normal/blank.pdf").toLocalFile());
        QVERIFY(source.open(QIODevice::ReadOnly));
        const auto original = source.readAll();
        for (int generation = 1; generation <= 2; ++generation)
        {
            const auto output =
                QUrl::fromLocalFile(directory.filePath(QString("graphics-%1.pdf").arg(generation)));
            document.saveAs(output);
            QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
            QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
#ifdef SAVE_NODE
            QProcess reader;
            reader.start(QString(SAVE_NODE),
                         {QString(TEST_ROOT) + "/tools/check_phase2_content.mjs",
                          QString(SAVE_PDFJS), output.toLocalFile()});
            QVERIFY(reader.waitForFinished(20000));
            const auto diagnostics = reader.readAllStandardOutput() + reader.readAllStandardError();
            QVERIFY2(reader.exitCode() == 0, diagnostics.constData());
#endif
        }
        source.seek(0);
        QCOMPARE(source.readAll(), original);
        const auto output = QUrl::fromLocalFile(directory.filePath("graphics-2.pdf"));
        document.open(output);
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QSignalSpy images(&document, &PdfDocument::rendered);
        document.requestRender(0, 612);
        QTRY_VERIFY_WITH_TIMEOUT(!images.isEmpty(), 10000);
        const auto rendered = qvariant_cast<QImage>(images.first()[1]);
        const auto hasInk = [&rendered](const QRect& area)
        {
            for (int y = area.top(); y <= area.bottom(); ++y)
                for (int x = area.left(); x <= area.right(); ++x)
                    if (qGray(rendered.pixel(x, y)) < 180)
                        return true;
            return false;
        };
        for (const auto& area :
             {QRect(40, 40, 32, 32), QRect(120, 40, 120, 70), QRect(280, 40, 120, 70),
              QRect(38, 148, 145, 55), QRect(255, 250, 140, 80)})
            QVERIFY(hasInk(area));
        const auto highlight = rendered.pixelColor(270, 170);
        QVERIFY(highlight.red() > 245 && highlight.green() > 245 && highlight.blue() > 150 &&
                highlight.blue() < 220);
        QCOMPARE(rendered.pixelColor(270, 400), QColor(Qt::magenta));
        QCOMPARE(rendered.pixelColor(110, 540), QColor(Qt::magenta));
        QCOMPARE(rendered.pixelColor(45, 505), QColor(Qt::white));
    }

    void additionUndoRedo()
    {
        PdfDocument document;
        document.open(fixture("normal/blank.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        auto* content = document.additions();
        QVERIFY(!content->canUndo());
        content->beginEdit();
        const int id = content->addText(0, 40, 50, "Original text");
        QVERIFY(content->setTextStyle(id, "Serif", 24, true, true, false));
        content->endEdit();
        const auto original = content->object(id);
        QVERIFY(document.dirty());
        content->undo();
        QCOMPARE(content->count(), 0);
        QVERIFY(!document.dirty());
        QVERIFY(!content->canUndo());
        QVERIFY(content->canRedo());
        content->redo();
        QCOMPARE(content->object(id), original);
        const auto originalRevision = content->revision();
        content->beginEdit();
        for (int x = 41; x < 80; ++x)
            QVERIFY(content->geometry(id, x, 60, 230, 80));
        QVERIFY(content->setText(id, "Moved and edited"));
        QVERIFY(content->setTextStyle(id, "Monospace", 20, false, false, true));
        QVERIFY(!content->canUndo());
        content->endEdit();
        const auto edited = content->object(id);
        content->undo();
        QCOMPARE(content->object(id), original);
        QCOMPARE(content->revision(), originalRevision);
        content->redo();
        QCOMPARE(content->object(id), edited);
        content->removeSelected();
        QCOMPARE(content->count(), 0);
        content->undo();
        QCOMPARE(content->object(id), edited);
        QCOMPARE(content->selected(), id);
        content->undo();
        QCOMPARE(content->object(id), original);
        QVERIFY(content->canRedo());
        const auto beforeInvalid = content->revision();
        QVERIFY(!content->setTextStyle(id, "Serif", 200, false, false, false));
        QCOMPARE(content->revision(), beforeInvalid);
        QVERIFY(content->canRedo());
        QVERIFY(content->setText(id, "New branch"));
        QVERIFY(!content->canRedo());
        QVERIFY(content->revision() > beforeInvalid);
        content->undo();
        QCOMPARE(content->object(id), original);
        content->redo();
        QCOMPARE(content->object(id)["text"].toString(), QString("New branch"));

        QTemporaryDir directory;
        QImage image(20, 20, QImage::Format_ARGB32);
        image.fill(Qt::magenta);
        const QString path = directory.filePath("signature.png");
        QVERIFY(image.save(path));
        for (bool signature : {false, true})
        {
            const int imageId = content->addImage(0, 40, 200, QUrl::fromLocalFile(path), signature);
            QVERIFY(imageId > 0);
            content->removeSelected();
            content->undo();
            QCOMPARE(content->selected(), imageId);
            QCOMPARE(content->objects().last().image, image);
            content->redo();
            QVERIFY(content->object(imageId).isEmpty());
        }
        QVERIFY(QFile::remove(path));
        content->undo();
        QCOMPARE(content->objects().last().image, image);
        for (int index = 0; index < 110; ++index)
            QVERIFY(content->setText(id, QString::number(index)));
        int undoCount = 0;
        while (content->canUndo())
        {
            content->undo();
            ++undoCount;
        }
        QCOMPARE(undoCount, 100);
        document.close();
        QVERIFY(!content->canUndo());
        QVERIFY(!content->canRedo());
    }

    void additionUndoSaveCheckpoint()
    {
        PdfDocument document;
        document.open(fixture("normal/blank.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        auto* content = document.additions();
        const int id = content->addText(0, 40, 50, "Saved baseline");
        QTemporaryDir directory;
        document.saveAs(QUrl::fromLocalFile(directory.filePath("saved.pdf")));
        const auto revision = content->revision();
        content->undo(); // History must not change the in-flight save snapshot.
        QCOMPARE(content->revision(), revision);
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
        QVERIFY(!document.dirty());
        QVERIFY(content->setTextStyle(id, "Serif", 36, true, false, false));
        QVERIFY(document.dirty());
        content->undo();
        QVERIFY(!document.dirty());
        content->redo();
        QVERIFY(document.dirty());
        document.saveAs(QUrl::fromLocalFile(directory.filePath("styled.pdf")));
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY(!document.dirty());
        content->undo();
        QVERIFY(document.dirty());
        content->redo();
        QVERIFY(!document.dirty());
    }

    void textFormattingAndPersistence()
    {
        PdfDocument document;
        document.open(fixture("normal/blank.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        auto* content = document.additions();
        const int id = content->addText(0, 50, 80, "Styled text");
        QVERIFY(id > 0);
        const auto originalRevision = content->revision();
        QVERIFY(!content->setTextStyle(id, "Serif", 0, true, true, true));
        QVERIFY(!content->setTextStyle(id, "Serif", 145, true, true, true));
        QVERIFY(!content->setTextStyle(id, "No such installed font 12345", 24, true, true, true));
        QCOMPARE(content->revision(), originalRevision);
        QVERIFY(content->setTextStyle(id, "Liberation Serif", 24, true, true, true));
        QCOMPARE(content->object(id)["fontSize"].toInt(), 24);
        const auto revision = content->revision();
        QVERIFY(content->setTextStyle(id, "Liberation Serif", 24, true, true, true));
        QCOMPARE(content->revision(), revision);
        QVERIFY(content->setText(id, "Styled text"));
        QCOMPARE(content->revision(), revision);
        AddedOverlay overlay;
        overlay.setContent(content);
        overlay.setProperty("page", 0);
        overlay.setProperty("selection", false);
        overlay.setWidth(612);
        overlay.setHeight(792);
        const auto preview = [&overlay]()
        {
            QImage image(612, 792, QImage::Format_ARGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            overlay.paint(&painter);
            return image;
        };
        const auto styled = preview();
        const auto longestLine = [](const QImage& image)
        {
            int longest = 0;
            for (int y = 80; y < 120; ++y)
            {
                int run = 0;
                for (int x = 50; x < 270; ++x)
                {
                    // A thin underline may cover only part of a pixel row in
                    // Qt 6.4. Count visible coverage rather than near-black ink.
                    run = qGray(image.pixel(x, y)) < 240 ? run + 1 : 0;
                    longest = std::max(longest, run);
                }
            }
            return longest;
        };
        QVERIFY(longestLine(styled) > 80); // Continuous underline, rather than glyph strokes.
        QVERIFY(content->setTextStyle(id, "Liberation Serif", 24, true, true, false));
        QVERIFY(longestLine(preview()) < 80); // Missing underlines must still fail the check.
        QVERIFY(content->setTextStyle(id, "Monospace", 12, false, false, false));
        QVERIFY(styled != preview());
        QVERIFY(content->setTextStyle(id, "Liberation Serif", 24, true, true, true));
        QTemporaryDir directory;
        const auto output = QUrl::fromLocalFile(directory.filePath("styled.pdf"));
        document.saveAs(output);
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 30000);
        QVERIFY2(document.saveError().isEmpty(), qPrintable(document.saveError()));
        QVERIFY(!document.dirty());
#ifdef SAVE_NODE
        QProcess reader;
        reader.start(QString(SAVE_NODE), {QString(TEST_ROOT) + "/tools/check_text_style.mjs",
                                          QString(SAVE_PDFJS), output.toLocalFile()});
        QVERIFY(reader.waitForFinished(20000));
        const auto diagnostics = reader.readAllStandardOutput() + reader.readAllStandardError();
        QVERIFY2(reader.exitCode() == 0, diagnostics.constData());
#endif
        document.open(output);
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QSignalSpy images(&document, &PdfDocument::rendered);
        document.requestRender(0, 612);
        QTRY_VERIFY_WITH_TIMEOUT(!images.isEmpty(), 10000);
        const auto rendered = qvariant_cast<QImage>(images.first()[1]);
        QVERIFY(rendered != QImage());
        QVERIFY(longestLine(rendered) > 80);
        bool ink = false;
        for (int y = 80; y < 110; ++y)
            for (int x = 50; x < 210; ++x)
                ink |= qGray(rendered.pixel(x, y)) < 100;
        QVERIFY(ink);
    }

    void rejectedOpenRetainsUnsavedDocument_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("pdf") << QString("normal/single-page.pdf");
        QTest::newRow("acroform") << QString("acroform/controls.pdf");
    }
    void rejectedOpenRetainsUnsavedDocument()
    {
        QFETCH(QString, path);
        PdfDocument document;
        document.open(fixture(path));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        const auto pid = document.workerPid();
        const auto editField = [&document](const QString& action, const QString& text = QString())
        {
            QSignalSpy done(&document, &PdfDocument::formEventFinished);
            QVERIFY(document.formEvent(0, action, 100, 87, 0, text));
            QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 10000);
        };
        if (path.startsWith("acroform"))
        {
            editField("click");
            editField("selectAll");
            editField("text", "retained form edit");
            QCOMPARE(document.formText(), QString("retained form edit"));
        }
        document.additions()->addText(0, 40, 40, "Keep this edit");
        QVERIFY(document.dirty());
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QFile oversized(temporary.filePath("oversized.pdf"));
        QVERIFY(oversized.open(QIODevice::WriteOnly));
        QVERIFY(oversized.resize(64 * 1024 * 1024 + 1));
        oversized.close();
        const QList<QUrl> rejected{QUrl("https://example.invalid/test.pdf"),
                                   QUrl::fromLocalFile(temporary.filePath("missing.pdf")),
                                   QUrl::fromLocalFile(temporary.path()),
                                   QUrl::fromLocalFile(oversized.fileName())};
        for (const auto& url : rejected)
        {
            document.open(url);
            QVERIFY(!document.error().isEmpty());
            QVERIFY(document.ready());
            QCOMPARE(document.workerPid(), pid);
            QCOMPARE(document.fileName(), QFileInfo(path).fileName());
            QCOMPARE(document.additions()->count(), 1);
            QVERIFY(document.dirty());
            if (path.startsWith("acroform"))
                QCOMPARE(document.formText(), QString("retained form edit"));
        }
        QSignalSpy images(&document, &PdfDocument::rendered);
        document.requestRender(0, 612);
        QTRY_VERIFY_WITH_TIMEOUT(!images.isEmpty(), 10000);
        const QString destination = temporary.filePath("retained.pdf");
        QSignalSpy saved(&document, &PdfDocument::saveFinished);
        document.saveAs(QUrl::fromLocalFile(destination));
        QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty(), 30000);
        QVERIFY2(saved.first()[0].toBool(), qPrintable(document.saveError()));
        document.open(QUrl::fromLocalFile(destination));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QVERIFY(document.error().isEmpty());
        if (path.startsWith("acroform"))
        {
            editField("click");
            QCOMPARE(document.formText(), QString("retained form edit"));
        }
    }

    void cancelledCachedRenderIsNotDelivered()
    {
        PdfDocument document;
        document.open(fixture("normal/single-page.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QSignalSpy images(&document, &PdfDocument::rendered);
        document.requestRender(0, 612);
        QTRY_VERIFY_WITH_TIMEOUT(!images.isEmpty(), 10000);
        images.clear();
        const auto cancelled = document.requestRender(0, 612);
        const auto retained = document.requestRender(0, 612);
        document.cancelRender(cancelled);
        QTRY_COMPARE_WITH_TIMEOUT(images.size(), 1, 10000);
        QCOMPARE(images.first()[0].toULongLong(), retained);
        QCoreApplication::processEvents();
        QCOMPARE(images.size(), 1);
        images.clear();
        document.requestRender(0, 612);
        document.close();
        QCoreApplication::processEvents();
        QVERIFY(images.isEmpty());
    }

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
        auto* family = item(window->contentItem(), "textFontFamily");
        auto* size = item(window->contentItem(), "textFontSize");
        auto* bold = item(window->contentItem(), "textBold");
        auto* italic = item(window->contentItem(), "textItalic");
        auto* underline = item(window->contentItem(), "textUnderline");
        QVERIFY(family && size && bold && italic && underline);
        family->setProperty("currentIndex", content->fontFamilies().indexOf("Serif"));
        size->setProperty("value", 30);
        bold->setProperty("checked", true);
        italic->setProperty("checked", true);
        underline->setProperty("checked", true);
        if (qEnvironmentVariableIsSet("PDF_TEXT_STYLE_SCREENSHOT"))
        {
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("PDF_TEXT_STYLE_SCREENSHOT")));
        }
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
        QCOMPARE(content->object(textId)["fontFamily"].toString(), QString("Serif"));
        QCOMPARE(content->object(textId)["fontSize"].toInt(), 30);
        QVERIFY(content->object(textId)["bold"].toBool());
        QVERIFY(content->object(textId)["italic"].toBool());
        QVERIFY(content->object(textId)["underline"].toBool());
        QVERIFY(click("editTextButton"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(size->property("value").toInt(), 30);
        QVERIFY(bold->property("checked").toBool());
        size->setProperty("value", 40);
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QCOMPARE(content->object(textId)["fontSize"].toInt(), 30);
        QVERIFY(click("editTextButton"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        size->setProperty("value", 20);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QCOMPARE(content->object(textId)["fontSize"].toInt(), 20);
        QVERIFY(click("undoAdditionsButton"));
        QCOMPARE(content->object(textId)["fontSize"].toInt(), 30);
        QVERIFY(click("redoAdditionsButton"));
        QCOMPARE(content->object(textId)["fontSize"].toInt(), 20);
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(content->object(textId)["fontSize"].toInt(), 30);
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(content->object(textId)["fontSize"].toInt(), 20);
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
            const auto beforeDrag = content->object(id);
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scene(60, 60));
            QTest::mouseMove(window, scene(75, 80), 20);
            QTest::mouseMove(window, scene(90, 100), 20);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, scene(90, 100));
            QTRY_VERIFY(content->object(id)["x"].toDouble() > 65);
            const auto before = content->object(id);
            QVERIFY(click("undoAdditionsButton"));
            QCOMPARE(content->object(id), beforeDrag);
            QVERIFY(click("redoAdditionsButton"));
            QCOMPARE(content->object(id), before);
            const double right =
                before["x"].toDouble() + before["width"].toDouble() - 2 / document.zoom();
            const double bottom =
                before["y"].toDouble() + before["height"].toDouble() - 2 / document.zoom();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scene(right, bottom));
            QTest::mouseMove(window, scene(right + 25, bottom + 20), 20);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
                                scene(right + 25, bottom + 20));
            QTRY_VERIFY(content->object(id)["width"].toDouble() > before["width"].toDouble() + 15);
            const auto resized = content->object(id);
            QVERIFY(click("undoAdditionsButton"));
            QCOMPARE(content->object(id), before);
            QVERIFY(click("redoAdditionsButton"));
            QCOMPARE(content->object(id), resized);
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
        QVERIFY(document.ready());
        QVERIFY(document.error().contains("readable PDF"));
        QCOMPARE(document.workerPid(), pid);
        document.close();
        QCOMPARE(kill(pid, 0), -1);
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
