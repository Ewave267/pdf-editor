// SPDX-License-Identifier: GPL-3.0-only
#include "pdf/PdfDocument.h"
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <csignal>
#include <unistd.h>

namespace
{
QUrl fixture(const QString& name)
{
    return QUrl::fromLocalFile(QString(TEST_ROOT) + "/tests/pdfs/" + name);
}
QList<qint64> processTree(qint64 root)
{
    QList<qint64> result{root};
    for (qsizetype index = 0; index < result.size(); ++index)
    {
        QFile children(QString("/proc/%1/task/%1/children").arg(result[index]));
        if (!children.open(QIODevice::ReadOnly))
            continue;
        for (auto value : children.readAll().trimmed().split(' '))
        {
            const auto child = value.toLongLong();
            if (child > 0 && !result.contains(child))
                result.append(child);
        }
    }
    return result;
}
qint64 residentBytes(qint64 pid)
{
    QFile status(QString("/proc/%1/status").arg(pid));
    if (!status.open(QIODevice::ReadOnly))
        return -1;
    for (auto line : status.readAll().split('\n'))
        if (line.startsWith("VmRSS:"))
            return line.simplified().split(' ')[1].toLongLong() * 1024;
    return -1;
}
} // namespace

class ViewerSafetyTests : public QObject
{
    Q_OBJECT
  private:
    void edit(PdfDocument& document)
    {
        document.formEvent(0, "click", 100, 87);
        QTRY_COMPARE(document.formText(), "original");
        document.formEvent(0, "selectAll");
        document.formEvent(0, "text", 0, 0, 0, "edited");
        QTRY_COMPARE(document.formText(), "edited");
        QTRY_VERIFY(!document.formBusy());
        document.additions()->addText(0, 50, 400, "Retain after failure");
    }
    void verifyReleased(const QList<qint64>& processes)
    {
        for (const auto pid : processes)
            QTRY_VERIFY_WITH_TIMEOUT(::kill(pid, 0) == -1, 3000);
    }
    void verifyRecovery(PdfDocument& document)
    {
        document.close();
        QVERIFY(!document.dirty());
        document.open(fixture("normal/single-page.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QSignalSpy image(&document, &PdfDocument::rendered);
        document.requestRender(0, 96);
        QTRY_VERIFY_WITH_TIMEOUT(!image.isEmpty(), 10000);
        document.close();
    }
  private slots:
    void malformedDocuments_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("invalid") << "malformed/not-a-pdf.pdf";
        QTest::newRow("corrupted") << "safety/corrupted.pdf";
        QTest::newRow("malformed-xfa") << "safety/broken-xfa.pdf";
    }
    void malformedDocuments()
    {
        QFETCH(QString, name);
        PdfDocument document;
        document.open(fixture(name));
        QTRY_VERIFY_WITH_TIMEOUT(!document.error().isEmpty(), 15000);
        QVERIFY(!document.ready());
        QVERIFY(!document.loading());
        QCOMPARE(document.workerPid(), 0);
        verifyRecovery(document);
    }
    void oversizedInput()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath("oversized.pdf"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(64 * 1024 * 1024 + 1));
        file.close();
        PdfDocument document;
        document.open(QUrl::fromLocalFile(file.fileName()));
        QVERIFY(document.error().contains("64 MiB"));
        QCOMPARE(document.workerPid(), 0);
        verifyRecovery(document);
    }
    void largeDocumentMemoryAndClose()
    {
        PdfDocument document;
        const auto initialHost = residentBytes(::getpid());
        QVERIFY(initialHost > 0);
        document.open(fixture("safety/many-pages.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        QCOMPARE(document.pageCount(), 1000);
        const auto processes = processTree(document.workerPid());
        QVERIFY(processes.size() >= 2);
        qint64 peakWorker = 0;
        for (int page = 0; page < 1000; page += 25)
        {
            QSignalSpy image(&document, &PdfDocument::rendered);
            document.requestRender(page, 612);
            QTRY_VERIFY_WITH_TIMEOUT(!image.isEmpty(), 10000);
            qint64 workerMemory = 0;
            for (const auto pid : processes)
                workerMemory += std::max<qint64>(0, residentBytes(pid));
            peakWorker = std::max(peakWorker, workerMemory);
            QVERIFY2(workerMemory < 384 * 1024 * 1024, "Large document exceeded worker RSS budget");
            QVERIFY(residentBytes(::getpid()) - initialHost < 160 * 1024 * 1024);
        }
        qInfo() << "1000-page peak worker RSS MiB:" << peakWorker / (1024 * 1024);
        document.close();
        QCOMPARE(document.workerPid(), 0);
        QCOMPARE(document.pageCount(), 0);
        verifyReleased(processes);
        QVERIFY(residentBytes(::getpid()) - initialHost < 80 * 1024 * 1024);
    }
    void formQueueIsBounded()
    {
        PdfDocument document;
        document.open(fixture("acroform/controls.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        for (int event = 0; event < 128; ++event)
            QVERIFY(document.formEvent(0, "copy"));
        QCOMPARE(document.formEvent(0, "copy"), 0);
        QVERIFY(document.formError().contains("busy"));
        document.close();
        QVERIFY(!document.formBusy());
        QCOMPARE(document.workerPid(), 0);
    }
    void rendererCrashRetainsAdditions()
    {
        PdfDocument document;
        document.open(fixture("acroform/controls.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        edit(document);
        const auto processes = processTree(document.workerPid());
        QCOMPARE(::kill(document.workerPid(), SIGKILL), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!document.error().isEmpty(), 5000);
        QVERIFY(!document.ready());
        QVERIFY(!document.formBusy());
        QVERIFY(document.error().contains("Unsaved form edits could not be recovered"));
        QCOMPARE(document.additions()->count(), 1);
        QVERIFY(document.dirty());
        verifyReleased(processes);
        verifyRecovery(document);
    }
    void startupScriptDeadline_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("acroform-loop") << "safety/loop-acroform.pdf";
        QTest::newRow("xfa-loop") << "safety/loop-xfa.pdf";
    }
    void startupScriptDeadline()
    {
        QFETCH(QString, name);
        PdfDocument document;
        int heartbeats = 0;
        QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&] { ++heartbeats; });
        timer.start(20);
        QElapsedTimer elapsed;
        elapsed.start();
        document.open(fixture(name));
        QTRY_VERIFY_WITH_TIMEOUT(!document.error().isEmpty(), 20000);
        QVERIFY(document.error().contains("too long"));
        QVERIFY(elapsed.elapsed() < 20000);
        QVERIFY(heartbeats > 100); // The host event loop stays responsive to the UI.
        QCOMPARE(document.workerPid(), 0);
        verifyRecovery(document);
    }
    void fieldScriptDeadline()
    {
        PdfDocument document;
        document.open(fixture("safety/exit-loop-xfa.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        edit(document);
        const auto processes = processTree(document.workerPid());
        document.formEvent(0, "key", 0, 0, 9);
        QTRY_VERIFY_WITH_TIMEOUT(!document.error().isEmpty(), 15000);
        QVERIFY(document.error().contains("too long"));
        QVERIFY(!document.formBusy());
        QCOMPARE(document.additions()->count(), 1);
        verifyReleased(processes);
        verifyRecovery(document);
    }
    void saveScriptDeadline()
    {
        PdfDocument document;
        document.open(fixture("safety/save-loop-acroform.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 15000);
        edit(document);
        const auto processes = processTree(document.workerPid());
        QTemporaryDir directory;
        const auto output = directory.filePath("failed.pdf");
        QSignalSpy finished(&document, &PdfDocument::saveFinished);
        document.saveAs(QUrl::fromLocalFile(output));
        QVERIFY(document.saving());
        QTRY_VERIFY_WITH_TIMEOUT(!document.saving(), 35000);
        QVERIFY(!document.saveError().isEmpty());
        QVERIFY(!QFile::exists(output));
        QCOMPARE(document.additions()->count(), 1);
        QVERIFY(document.dirty());
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished.first()[0].toBool());
        verifyReleased(processes);
        verifyRecovery(document);
    }
};
QTEST_MAIN(ViewerSafetyTests)
#include "test_viewer_safety.moc"
