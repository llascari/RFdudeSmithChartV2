#include "DocumentIO.h"
#include "Editors.h"
#include "MainWindow.h"
#include "LtspiceExport.h"
#include "SmithChartWidget.h"
#include "SchematicWidget.h"
#include "FrequencyResponseDialog.h"
#include "ResponsePlotWidget.h"
#include "Synthesis.h"
#include "Touchstone.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QIcon>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QScopeGuard>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
QAction* action(MainWindow& window, const char* name)
{
    auto* result = window.findChild<QAction*>(name);
    if (!result) qFatal("Missing action: %s", name);
    return result;
}

void present(MainWindow& window)
{
    window.show();
    window.activateWindow();
    window.raise();
    QTest::qWait(30);
}

void acceptEditor(MainWindow& window, double value, bool shunt = false)
{
    QTimer::singleShot(0, &window, [value, shunt] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) qFatal("Expected component editor.");
        auto* connection = dialog->findChild<QComboBox*>("componentConnection");
        auto* capacitance = dialog->findChild<QDoubleSpinBox*>("capacitance");
        auto* box = dialog->findChild<QDialogButtonBox*>();
        if (!connection || !capacitance || !box) qFatal("Missing editor controls.");
        connection->setCurrentIndex(shunt ? 1 : 0);
        capacitance->setValue(value);
        box->button(QDialogButtonBox::Ok)->click();
    });
}
}

class UiTests final : public QObject
{
    Q_OBJECT

private slots:
    void applicationIconResource()
    {
        const QIcon icon(":/icons/rfdude-smith-chart-v2.png");
        QVERIFY(!icon.isNull());
        for (const int size : {16, 32, 48, 64, 128, 256}) {
            const auto pixmap = icon.pixmap(size, size);
            QVERIFY(!pixmap.isNull());
            QCOMPARE(pixmap.size(), QSize(size, size));
            const auto image = pixmap.toImage();
            QVERIFY(image.pixelColor(size / 2, size / 2).alpha() > 0);
            QVERIFY(image.pixelColor(0, 0).alpha() < 255);
        }
    }

    void aboutCreditsAndReferences()
    {
        MainWindow window;
        QString aboutText;
        QTimer::singleShot(0, &window, [&aboutText] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) qFatal("Expected About message box.");
            aboutText = box->text();
            box->accept();
        });
        action(window, "about")->trigger();

        QVERIFY(aboutText.contains("Built with Qt "));
        QVERIFY(aboutText.contains("Hammerstad and Jensen"));
        QVERIFY(aboutText.contains("Qucs Technical Papers"));
        QVERIFY(aboutText.contains("Pozar"));
        QVERIFY(aboutText.contains("IBIS Open Forum"));
        QVERIFY(aboutText.contains("doi.org"));
    }

    void touchstoneImportAndCombinedPng()
    {
        MainWindow window;
        present(window);
        QTemporaryDir directory;
        const auto twoPort = directory.filePath("measured-amplifier.s2p");
        const auto onePort = directory.filePath("antenna.s1p");
        {
            QFile file(twoPort);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("# MHz S RI R 75\n500 .1 0 2 0 .1 0 -.2 0\n"
                               "1500 .3 0 4 0 .2 0 -.4 0\n") > 0);
        }
        {
            QFile file(onePort);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("# MHz S RI R 50\n500 .5 0\n1500 0 .5\n") > 0);
        }
        QVERIFY(window.importTouchstone(twoPort));
        QCOMPARE(window.document().components.size(), std::size_t(1));
        QCOMPARE(window.document().components.front().kind, rf::Kind::Measured);
        QVERIFY(window.isModified());
        auto* response = window.findChild<FrequencyResponseDialog*>();
        auto* plot = response->findChild<ResponsePlotWidget*>();
        auto* mode = response->findChild<QComboBox*>("responseMode");
        auto* start = response->findChild<QDoubleSpinBox*>("sweepStart");
        auto* apply = response->findChild<QPushButton*>("applySweep");
        QVERIFY(response->isVisible());
        QCOMPARE(mode->currentIndex(), 0);
        QCOMPARE(plot->samples().front().frequencyMHz, 500.0);
        QCOMPARE(plot->samples().back().frequencyMHz, 1500.0);
        QVERIFY(plot->pointForSample(1000, 10).y() < plot->pointForSample(1000, 0).y());
        auto* schematic = window.findChild<SchematicWidget*>();
        const auto cell = schematic->componentRect(0);
        const auto hoverPoint = schematic->mapFromScene(cell.center());
        QMouseEvent hover(QEvent::MouseMove, hoverPoint, schematic->viewport()->mapToGlobal(hoverPoint),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(schematic->viewport(), &hover);
        QVERIFY(schematic->toolTip().contains("measured-amplifier.s2p"));
        QVERIFY(schematic->toolTip().contains("P1=input / P2=load"));
        auto report = window.chartImage();
        const int reportWidth = report.width() - 40;
        const int responseTop = window.findChild<SmithChartWidget*>()->height()
            + schematic->reportHeight(reportWidth) + 60;
        auto responsePng = response->responseImage().scaledToWidth(reportWidth, Qt::SmoothTransformation);
        QCOMPARE(report.copy(20, responseTop, responsePng.width(), responsePng.height()), responsePng);
        // Unapplied controls must not leak into either report.
        start->setValue(700);
        QCOMPARE(window.chartImage(), report);
        apply->click();
        QVERIFY(window.chartImage() != report);
        response->hide();
        const auto hiddenReport = window.chartImage();
        QVERIFY(!response->isVisible());
        QCOMPARE(window.chartImage(), hiddenReport);
        QVERIFY(window.importTouchstone(onePort));
        QVERIFY(window.document().measuredLoad.has_value());
        QCOMPARE(mode->currentIndex(), 1);
        const auto expected = rf::inputReflection(window.document(), 1000, 50);
        QVERIFY(std::abs(rf::evaluate(window.document()).nodes.back().reflection(50) - expected) < 1e-10);
        QVERIFY(response->findChild<QLabel*>("responseMarker")->text().contains("Terminated input S11"));
        QVERIFY(!response->findChild<QLabel*>("responseMarker")->text().contains("S21"));
        mode->setCurrentIndex(0);
        apply->click();
        QVERIFY(response->findChild<QLabel*>("responseMarker")->text().contains("S21"));
        const auto before = window.document();
        const auto project = directory.filePath("embedded.rfs.json");
        QVERIFY(window.saveDocument(project));
        QVERIFY(QFile::remove(twoPort));
        QVERIFY(QFile::remove(onePort));
        QVERIFY(window.openDocument(project));
        QCOMPARE(window.document(), before);
        // Both numerical data and schematic render survive removal of original files.
        report = window.chartImage();
        QVERIFY(!report.isNull());
        const auto png = directory.filePath("combined.png");
        QVERIFY(report.save(png, "PNG"));
        QCOMPARE(QImage(png).convertToFormat(report.format()), report);
        const auto screenshot = qEnvironmentVariable("RFDUDE_TOUCHSTONE_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            QVERIFY(report.save(screenshot, "PNG"));
            QVERIFY(window.grab().save(screenshot + ".window.png", "PNG"));
        }
        action(window, "clearMeasuredLoad")->trigger();
        QVERIFY(!window.document().measuredLoad);
        action(window, "undo")->trigger();
        QCOMPARE(window.document(), before);
        action(window, "deleteComponent")->trigger();
        QVERIFY(window.document().components.empty());
        QCOMPARE(mode->currentIndex(), 1);
        const auto loadOnly = rf::measuredParameters(*before.measuredLoad, 1000, 50).s11;
        QVERIFY(std::abs(rf::inputReflection(window.document(), 1000, 50) - loadOnly) < 1e-10);
    }

    void touchstoneErrorsAndUndo()
    {
        MainWindow window;
        present(window);
        QTemporaryDir directory;
        const auto path = directory.filePath("fixture.s2p");
        const auto write = [&](const QByteArray& bytes) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
                qFatal("Could not write Touchstone fixture.");
        };
        write("# MHz S RI R 50\n2000 0 0 .5 0 .5 0 0 0\n3000 0 0 .5 0 .5 0 0 0\n");
        QVERIFY(window.importTouchstone(path));
        QCOMPARE(window.document().frequencyMHz, 2000.0);
        action(window, "undo")->trigger();
        QCOMPARE(window.document().frequencyMHz, 1000.0);
        QVERIFY(window.document().components.empty());
        action(window, "redo")->trigger();
        const auto before = window.document();
        const auto dismissError = [&] {
            QTimer::singleShot(0, &window, [] {
                auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (!box) qFatal("Expected an explicit error notification.");
                box->accept();
            });
        };
        write("[Version] 2.0\n");
        dismissError();
        QVERIFY(!window.importTouchstone(path));
        QCOMPARE(window.document(), before);
        write("# MHz S RI R 50\n4000 0 0 1 0 1 0 0 0\n5000 0 0 1 0 1 0 0 0\n");
        dismissError();
        QVERIFY(!window.importTouchstone(path));
        QCOMPARE(window.document(), before);
        dismissError();
        action(window, "decrease_frequency")->trigger();
        QCOMPARE(window.document(), before);
        auto* response = window.findChild<FrequencyResponseDialog*>();
        response->findChild<QDoubleSpinBox*>("sweepStart")->setValue(1000);
        response->findChild<QPushButton*>("applySweep")->click();
        QVERIFY(response->findChild<ResponsePlotWidget*>()->samples().empty());
        QVERIFY_EXCEPTION_THROWN(window.chartImage(), std::invalid_argument);
        QVERIFY(response->findChild<QLabel*>("responseStatus")->text().contains("No extrapolation"));
        response->findChild<QDoubleSpinBox*>("sweepStart")->setValue(2000);
        response->findChild<QPushButton*>("applySweep")->click();
        QVERIFY(!window.chartImage().isNull());
    }

    void touchstoneSingleFrequency()
    {
        MainWindow window;
        present(window);
        QTemporaryDir directory;
        const auto path = directory.filePath("one-point.s1p");
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("# MHz S RI R 50\n1234 0 .5\n") > 0);
        }
        QVERIFY(window.importTouchstone(path));
        QCOMPARE(window.document().frequencyMHz, 1234.0);
        auto* response = window.findChild<FrequencyResponseDialog*>();
        auto* plot = response->findChild<ResponsePlotWidget*>();
        QCOMPARE(plot->samples().size(), std::size_t(1));
        QCOMPARE(plot->samples().front().frequencyMHz, 1234.0);
        QVERIFY(std::abs(plot->samples().front().s.s11 - rf::Complex(0, .5)) < 1e-10);
        QVERIFY(response->findChild<QLabel*>("responseStatus")->text().contains("Single measured frequency"));
        QVERIFY(!window.chartImage().isNull());
        action(window, "undo")->trigger();
        QVERIFY(!window.document().measuredLoad);
        QVERIFY(!window.chartImage().isNull());
        action(window, "redo")->trigger();
        QCOMPARE(plot->samples().size(), std::size_t(1));
    }

    void frequencyResponseControlsAndMarker()
    {
        MainWindow window;
        rf::FilterSpecification specification;
        specification.order = 5;
        specification.rippleDb = 0.5;
        specification.cutoffMHz = 1000;
        auto document = rf::synthesizeFilter(specification);
        document.startOhms = {73, -21};
        document.goalOhms = {90, 50};
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("response.rfs.json");
        documents::save(path, document);
        QVERIFY(window.openDocument(path));
        present(window);
        action(window, "frequencyResponse")->trigger();
        auto* dialog = window.findChild<FrequencyResponseDialog*>();
        QVERIFY(dialog && dialog->isVisible());
        QVERIFY(!dialog->isModal());
        auto* plot = dialog->findChild<ResponsePlotWidget*>();
        auto* marker = dialog->findChild<QLabel*>("responseMarker");
        QVERIFY(plot && marker);
        QCOMPARE(plot->samples().size(), std::size_t(401));
        QVERIFY(!window.isModified());
        dialog->findChild<QDoubleSpinBox*>("sweepStart")->setValue(100);
        dialog->findChild<QDoubleSpinBox*>("sweepStop")->setValue(4000);
        dialog->findChild<QDoubleSpinBox*>("sweepPorts")->setValue(75);
        dialog->findChild<QSpinBox*>("sweepPoints")->setValue(101);
        dialog->findChild<QComboBox*>("sweepScale")->setCurrentIndex(1);
        dialog->findChild<QPushButton*>("applySweep")->click();
        QCOMPARE(plot->samples().size(), std::size_t(101));
        QCOMPARE(plot->samples().front().frequencyMHz, 100.0);
        QCOMPARE(plot->samples().back().frequencyMHz, 4000.0);
        const auto expected = rf::sParameters(document.components, plot->samples()[40].frequencyMHz, 75);
        QCOMPARE(plot->samples()[40].s.s21, expected.s21);
        QVERIFY(!window.isModified());
        const auto beforeImage = dialog->responseImage();
        QVERIFY(!beforeImage.isNull());
        QCOMPARE(beforeImage.width(), std::max(900, plot->width() + 40));
        QCOMPARE(beforeImage.height(), plot->height() + 240);
        dialog->findChild<QDoubleSpinBox*>("sweepStop")->setValue(8000);
        QCOMPARE(dialog->responseImage(), beforeImage);
        const auto before = window.document();
        const auto point = plot->pointForSample(2000, -30).toPoint();
        QTest::mouseClick(plot, Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(std::abs(window.document().frequencyMHz - 2000) < 20);
        QVERIFY(window.isModified());
        auto changed = window.document();
        changed.frequencyMHz = before.frequencyMHz;
        QVERIFY(changed == before);
        const auto s = rf::sParameters(document.components, window.document().frequencyMHz, 75);
        QVERIFY(marker->text().contains(QString::number(20 * std::log10(std::abs(s.s21)), 'f', 3)));
        QVERIFY(dialog->responseImage() != beforeImage);
        action(window, "undo")->trigger();
        QVERIFY(window.document() == before);
        QVERIFY(!window.isModified());
        QCOMPARE(dialog->responseImage(), beforeImage);
        action(window, "redo")->trigger();
        QVERIFY(window.isModified());
        action(window, "undo")->trigger();

        const auto png = directory.filePath("response.png");
        QVERIFY(beforeImage.save(png, "PNG"));
        QCOMPARE(QImage(png).convertToFormat(beforeImage.format()), beforeImage);
        const auto screenshot = qEnvironmentVariable("RFDUDE_RESPONSE_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            QVERIFY(dialog->responseImage().save(screenshot, "PNG"));
            QVERIFY(dialog->grab().save(screenshot + ".window.png", "PNG"));
        }
        dialog->hide();
        action(window, "increase_component")->trigger();
        const double oldSample = std::abs(plot->samples()[40].s.s21);
        action(window, "frequencyResponse")->trigger();
        QVERIFY(dialog->isVisible());
        QVERIFY(std::abs(plot->samples()[40].s.s21) != oldSample);
        QCOMPARE(plot->samples().size(), std::size_t(101));
        action(window, "undo")->trigger();
        QCOMPARE(dialog->responseImage(), beforeImage);
        dialog->close();
        QVERIFY(!dialog->isVisible());
        action(window, "viewFrequencyResponse")->trigger();
        QCOMPARE(window.findChildren<FrequencyResponseDialog*>().size(), qsizetype(1));
        QCOMPARE(dialog->responseImage(), beforeImage);
        document.startOhms = {15, 40};
        document.goalOhms = {500, -80};
        document.referenceOhms = 93;
        documents::save(path, document);
        QVERIFY(window.openDocument(path));
        QCOMPARE(dialog->responseImage(), beforeImage);
        action(window, "new")->trigger();
        QCOMPARE(plot->samples().size(), std::size_t(101));
        QCOMPARE(plot->samples()[40].s.s21, rf::Complex(1));
        QCOMPARE(plot->samples()[40].s.s11, rf::Complex{});
        QVERIFY(!window.isModified());
    }

    void frequencyResponseErrorsAndEmpty()
    {
        MainWindow window;
        present(window);
        action(window, "frequencyResponse")->trigger();
        auto* dialog = window.findChild<FrequencyResponseDialog*>();
        auto* plot = dialog->findChild<ResponsePlotWidget*>();
        auto* marker = dialog->findChild<QLabel*>("responseMarker");
        auto* status = dialog->findChild<QLabel*>("responseStatus");
        QCOMPARE(plot->samples().front().s.s21, rf::Complex(1));
        QVERIFY(marker->text().contains("S11: -inf dB"));
        QVERIFY(marker->text().contains("S21: 0.000 dB"));
        auto* start = dialog->findChild<QDoubleSpinBox*>("sweepStart");
        auto* stop = dialog->findChild<QDoubleSpinBox*>("sweepStop");
        auto* apply = dialog->findChild<QPushButton*>("applySweep");
        start->setValue(2000);
        stop->setValue(2000);
        apply->click();
        QVERIFY(plot->samples().empty());
        QVERIFY(status->text().contains("Cannot calculate"));
        QVERIFY(marker->text().isEmpty());
        QVERIFY_EXCEPTION_THROWN(dialog->responseImage(), std::invalid_argument);
        const double frequency = window.document().frequencyMHz;
        QTest::mouseClick(plot, Qt::LeftButton, Qt::NoModifier, plot->rect().center());
        QCOMPARE(window.document().frequencyMHz, frequency);
        stop->setValue(4000);
        apply->click();
        QVERIFY(!plot->samples().empty());
        QVERIFY(marker->text().contains("outside sweep"));
        auto* ports = dialog->findChild<QDoubleSpinBox*>("sweepPorts");
        ports->setValue(75);
        apply->click();
        action(window, "increase_Z0")->trigger();
        QCOMPARE(ports->value(), 75.0);
        action(window, "undo")->trigger();
        QVERIFY(!window.isModified());
        dialog->hide();
    }

    void newElementEditors()
    {
        MainWindow window;
        present(window);
        for (const auto& shortcut : {"addI", "addK", "addB"}) {
            bool previewed = false;
            QTimer::singleShot(0, &window, [&] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dialog) qFatal("Expected component editor.");
                if (QString(shortcut) == "addI") {
                    dialog->findChild<QDoubleSpinBox*>("turnsRatio")->setValue(2);
                    previewed = dialog->findChild<QLabel*>("linePreview")->text().contains("4 x Z(load)");
                } else if (QString(shortcut) == "addK") {
                    dialog->findChild<QComboBox*>("lengthMode")->setCurrentIndex(1);
                    dialog->findChild<QComboBox*>("velocityMode")->setCurrentIndex(1);
                    dialog->findChild<QDoubleSpinBox*>("physicalLength")->setValue(50);
                    dialog->findChild<QDoubleSpinBox*>("dielectricConstant")->setValue(2.25);
                    previewed = dialog->findChild<QLabel*>("linePreview")->text().contains("Electrical length");
                } else {
                    QCOMPARE(dialog->findChild<QDoubleSpinBox*>("substrateDk")->value(), 4.2);
                    QCOMPARE(dialog->findChild<QDoubleSpinBox*>("substrateHeight")->value(), 1.5);
                    QCOMPARE(dialog->findChild<QDoubleSpinBox*>("lossTangent")->value(), 0.02);
                    dialog->findChild<QDoubleSpinBox*>("targetImpedance")->setValue(75);
                    dialog->findChild<QPushButton*>("calculateWidth")->click();
                    const double width = dialog->findChild<QDoubleSpinBox*>("stripWidth")->value();
                    QVERIFY(std::abs(rf::microstrip(width, 1.5, 4.2).impedanceOhms - 75) < 1e-6);
                    auto* box = dialog->findChild<QDialogButtonBox*>();
                    dialog->findChild<QDoubleSpinBox*>("stripWidth")->setValue(1000);
                    QVERIFY(!box->button(QDialogButtonBox::Ok)->isEnabled());
                    QVERIFY(dialog->findChild<QLabel*>("linePreview")->text().contains("Invalid"));
                    auto* kind = dialog->findChild<QComboBox*>("componentKind");
                    kind->setCurrentIndex(kind->findData(int(rf::Kind::Capacitor)));
                    QVERIFY(box->button(QDialogButtonBox::Ok)->isEnabled());
                    kind->setCurrentIndex(kind->findData(int(rf::Kind::Microstrip)));
                    QVERIFY(!box->button(QDialogButtonBox::Ok)->isEnabled());
                    dialog->findChild<QDoubleSpinBox*>("stripWidth")->setValue(width);
                    QVERIFY(box->button(QDialogButtonBox::Ok)->isEnabled());
                    auto* preview = dialog->findChild<QLabel*>("linePreview");
                    previewed = preview->text().contains("dielectric loss");
                    dialog->adjustSize();
                    QVERIFY(dialog->width() >= 640);
                    QVERIFY(preview->width() >= 580);
                    QVERIFY(preview->text().contains("\nEffective Dk:"));
                    auto* notes = dialog->findChild<QGroupBox*>("microstripModelNotes");
                    QVERIFY(notes && notes->isVisible());
                    QVERIFY(preview->mapTo(dialog, QPoint(0, preview->height())).y()
                            < notes->mapTo(dialog, QPoint(0, 0)).y());
                    const auto screenshot = qEnvironmentVariable("RFDUDE_ELEMENT_SCREENSHOT");
                    if (!screenshot.isEmpty()) {
                        dialog->adjustSize();
                        QVERIFY(dialog->grab().save(screenshot + ".editor.png", "PNG"));
                    }
                }
                QTest::keyClick(dialog, Qt::Key_Return);
                if (dialog->isVisible()) dialog->reject();
            });
            action(window, shortcut)->trigger();
            QVERIFY(previewed);
        }
        QCOMPARE(window.document().components.size(), std::size_t(3));
        QCOMPARE(window.document().components[0].turnsRatio, 2.0);
        QCOMPARE(window.document().components[1].lengthMode, rf::LengthMode::Physical);
        QCOMPARE(window.document().components[1].velocityMode, rf::VelocityMode::Permittivity);
        QCOMPARE(window.document().components[1].physicalLengthMm, 50.0);
        QCOMPARE(window.document().components[2].kind, rf::Kind::Microstrip);
        const auto beforeTune = window.document();
        action(window, "increase_component")->trigger();
        QCOMPARE(window.document().components[2].physicalLengthMm, 10.25);
        action(window, "undo")->trigger();
        QVERIFY(window.document() == beforeTune);
        const auto image = window.chartImage();
        QVERIFY(!image.isNull());
        const auto screenshot = qEnvironmentVariable("RFDUDE_ELEMENT_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(image.save(screenshot, "PNG"));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("elements.rfs.json");
        QVERIFY(window.saveDocument(path));
        QVERIFY(window.openDocument(path));
        QVERIFY(window.document() == beforeTune);
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected microstrip calculator.");
            dialog->reject();
        });
        action(window, "microstripCalculator")->trigger();
        QVERIFY(window.document() == beforeTune);
        QVERIFY(!window.isModified());
    }

    void schematicReportLayout_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("count");
        for (const int width : {460, 700, 960, 1200})
            for (const int count : {0, 1, 3, 4, 5, 6, 8})
                QTest::newRow(qPrintable(QString("%1px-%2-elements").arg(width).arg(count)))
                    << width << count;
    }

    void schematicReportLayout()
    {
        QFETCH(int, width);
        QFETCH(int, count);
        SchematicWidget schematic;
        rf::Document document;
        document.components.resize(count, rf::defaultComponent(rf::Kind::Resistor, 1000, 50));
        document.selected = count ? 0 : -1;
        schematic.setNetwork(document);
        QVERIFY_EXCEPTION_THROWN(schematic.reportHeight(249), std::invalid_argument);
        const auto render = [&] {
            QImage image(width, schematic.reportHeight(width), QImage::Format_RGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            schematic.paintReport(painter, {}, width);
            return image;
        };
        const auto before = render();
        QVERIFY(!before.isNull());
        QCOMPARE(before.height() % 232, 0);
        if (count == 0) QCOMPARE(before.height(), 232);
        if (count <= 5 && width >= 960) QCOMPARE(before.height(), 232);
        if (count == 6 && width == 1200) QCOMPARE(before.height(), 232);
        if (count == 8 && width == 1200) QCOMPARE(before.height(), 464);
        if (count > 1) {
            document.selected = count - 1;
            schematic.setNetwork(document);
            QCOMPARE(render(), before);
        }
        for (int i = 0; i < count; ++i) {
            document.components[static_cast<std::size_t>(i)].resistance = 75;
            schematic.setNetwork(document);
            const auto changed = render();
            QCOMPARE(changed.size(), before.size());
            int changedBands = 0;
            for (int y = 0; y < before.height(); y += 232)
                changedBands += changed.copy(0, y, width, 232) != before.copy(0, y, width, 232);
            QCOMPARE(changedBands, 1);
            document.components[static_cast<std::size_t>(i)].resistance = 50;
        }
    }

    void schematicTopologies()
    {
        SchematicWidget schematic;
        schematic.resize(900, 340);
        schematic.show();
        rf::Document document;
        QImage previous;
        const auto screenshot = qEnvironmentVariable("RFDUDE_SCHEMATIC_SCREENSHOT");
        QImage samples;
        if (!screenshot.isEmpty()) {
            samples = QImage(4 * 140, 6 * 200, QImage::Format_RGB32);
            samples.fill(Qt::white);
        }
        int sample = 0;
        for (const auto kind : {rf::Kind::Resistor, rf::Kind::Inductor, rf::Kind::Capacitor,
                                rf::Kind::SeriesRlc, rf::Kind::ParallelRlc, rf::Kind::Line,
                                rf::Kind::Coax, rf::Kind::Microstrip, rf::Kind::Transformer}) {
            const auto connections = rf::isTransmissionLine(kind)
                ? std::vector{rf::Connection::ThroughLine, rf::Connection::OpenStub, rf::Connection::ShortStub}
                : kind == rf::Kind::Transformer ? std::vector{rf::Connection::ThroughLine}
                : std::vector{rf::Connection::Series, rf::Connection::Shunt};
            for (const auto connection : connections) {
                auto c = rf::defaultComponent(kind, 1000, 50);
                c.connection = connection;
                document.components = {c};
                document.selected = 0;
                schematic.setNetwork(document);
                const auto bounds = schematic.componentRect(0);
                QCOMPARE(bounds, QRectF(110, 0, 140, 200));
                const auto image = schematic.viewport()->grab().toImage();
                const auto crop = image.copy(schematic.mapFromScene(bounds).boundingRect());
                QVERIFY(!crop.isNull());
                QVERIFY(crop != previous);
                previous = crop;
                if (!samples.isNull()) {
                    QPainter painter(&samples);
                    painter.drawImage(QPoint((sample % 4) * 140, (sample / 4) * 200), crop);
                }
                ++sample;

                // Every topology has connected signal wires at both cell boundaries.
                for (const double x : {bounds.left() + 1, bounds.right() - 1}) {
                    const auto point = schematic.mapFromScene(QPointF(x, 60));
                    QVERIFY(image.pixelColor(point) != schematic.palette().highlight().color());
                }
                if (connection == rf::Connection::Shunt || connection == rf::Connection::ShortStub) {
                    const auto point = schematic.mapFromScene(QPointF(bounds.center().x(), 130));
                    QVERIFY(image.pixelColor(point) != schematic.palette().highlight().color());
                }
                if (connection == rf::Connection::OpenStub) {
                    const auto point = schematic.mapFromScene(QPointF(bounds.center().x(), 136));
                    QCOMPARE(image.pixelColor(point), schematic.palette().highlight().color());
                }
            }
        }
        document.components.front() = rf::defaultComponent(rf::Kind::Line, 1000, 50);
        schematic.setNetwork(document);
        const auto bounds = schematic.componentRect(0);
        previous = schematic.viewport()->grab().toImage().copy(
            schematic.mapFromScene(bounds).boundingRect());
        const auto before = document;
        document.components.front().lengthDegrees = 0;
        schematic.setNetwork(document);
        QVERIFY(schematic.viewport()->grab().toImage().copy(
            schematic.mapFromScene(bounds).boundingRect()) != previous);
        QCOMPARE(document.components.front().connection, before.components.front().connection);
        if (!screenshot.isEmpty()) QVERIFY(samples.save(screenshot, "PNG"));
    }

    void schematicSelectionAndEditing()
    {
        MainWindow window;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        rf::Document document;
        for (int i = 0; i < 3; ++i)
            document.components.push_back(rf::defaultComponent(rf::Kind::Capacitor, 1000, 50));
        document.selected = 0;
        const auto path = directory.filePath("schematic.rfs.json");
        documents::save(path, document);
        QVERIFY(window.openDocument(path));
        present(window);
        auto* schematic = window.findChild<SchematicWidget*>();
        auto* table = window.findChild<QTableWidget*>();
        QVERIFY(schematic);
        QVERIFY(table);
        const auto screenshot = qEnvironmentVariable("RFDUDE_SCHEMATIC_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(window.grab().save(screenshot + ".window.png", "PNG"));
        const auto click = schematic->mapFromScene(QPointF(schematic->componentRect(1).center().x(), 60));
        QVERIFY(schematic->viewport()->rect().contains(click));
        QVERIFY(schematic->componentRect(2).left() < schematic->componentRect(1).left());
        QVERIFY(schematic->componentRect(1).left() < schematic->componentRect(0).left());
        const auto unchanged = window.document();
        const auto image = schematic->viewport()->grab().toImage();
        QTest::mouseClick(schematic->viewport(), Qt::LeftButton, Qt::NoModifier, click);
        QMouseEvent hover(QEvent::MouseMove, click, schematic->viewport()->mapToGlobal(click),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(schematic->viewport(), &hover);
        QVERIFY(schematic->toolTip().contains("#2 Series"));
        QVERIFY(schematic->toolTip().contains("3.183099 pF"));
        QCOMPARE(window.document().selected, 1);
        QCOMPARE(table->currentRow(), 1);
        QVERIFY(!window.isModified());
        QVERIFY(schematic->viewport()->grab().toImage() != image);
        auto selected = window.document();
        selected.selected = unchanged.selected;
        QVERIFY(selected == unchanged);
        QCOMPARE(window.document().components, document.components);

        acceptEditor(window, 27);
        QTest::mouseDClick(schematic->viewport(), Qt::LeftButton, Qt::NoModifier, click);
        QCOMPARE(window.document().components[1].capacitancePf, 27.0);
        QVERIFY(window.isModified());
        action(window, "undo")->trigger();
        QCOMPARE(window.document().components, document.components);
        QVERIFY(!window.isModified());
        action(window, "redo")->trigger();
        QCOMPARE(window.document().components[1].capacitancePf, 27.0);
        present(window);
        schematic->setFocus();
        QTest::keyClick(schematic, Qt::Key_Up);
        QCOMPARE(window.document().components[1].capacitancePf, 27.675);
        table->selectRow(2);
        QCOMPARE(window.document().selected, 2);
        QTest::keyClick(schematic, Qt::Key_Right);
        QCOMPARE(window.document().selected, 0);
        QTest::keyClick(schematic, Qt::Key_Delete);
        QCOMPARE(window.document().components.size(), std::size_t(2));
        QVERIFY(schematic->componentRect(2).isNull());
        action(window, "undo")->trigger();
        QVERIFY(!schematic->componentRect(2).isNull());
    }

    void schematicEmptyAndLongNetworks()
    {
        MainWindow window;
        present(window);
        auto* schematic = window.findChild<SchematicWidget*>();
        auto* splitter = window.findChild<QSplitter*>("workspaceSplitter");
        QVERIFY(schematic);
        QVERIFY(splitter);
        const auto empty = schematic->viewport()->grab().toImage();
        QVERIFY(!empty.isNull());
        QVERIFY(schematic->componentRect(0).isNull());
        QTest::mouseDClick(schematic->viewport(), Qt::LeftButton, Qt::NoModifier, {50, 95});
        QVERIFY(!window.isModified());
        QVERIFY(!QApplication::activeModalWidget());
        rf::Document document;
        document.selected = 0;
        document.components.resize(1000, rf::defaultComponent(rf::Kind::Resistor, 1000, 50));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("long.rfs.json");
        documents::save(path, document);
        QVERIFY(window.openDocument(path));
        QCoreApplication::processEvents();
        QVERIFY(schematic->horizontalScrollBar()->maximum() > 0);
        const auto isVisible = [&](int index) {
            return schematic->viewport()->rect().contains(
                schematic->mapFromScene(QPointF(schematic->componentRect(index).center().x(), 60)));
        };
        QVERIFY(isVisible(0));
        action(window, "previous")->trigger();
        QVERIFY(isVisible(999));
        action(window, "next")->trigger();
        QVERIFY(isVisible(0));
        auto* scroll = schematic->horizontalScrollBar();
        scroll->setValue(scroll->maximum() / 2);
        const auto position = scroll->value();
        const auto beforeTune = window.document();
        action(window, "increase_component")->trigger();
        QCOMPARE(scroll->value(), position);
        action(window, "undo")->trigger();
        QVERIFY(window.document() == beforeTune);
        QCOMPARE(scroll->value(), position);
        QVERIFY(!window.isModified());
        QVERIFY(window.saveDocument(path));
        QVERIFY(window.openDocument(path));
        QCOMPARE(window.document().components, document.components);
        window.resize(1250, 1100);
        splitter->setSizes({400, 400});
        QCoreApplication::processEvents();
        QVERIFY(schematic->height() >= 350);
        QVERIFY(schematic->viewport()->grab().toImage() != empty);
        QVERIFY(window.openDocument(directory.filePath("long.rfs.json")));
        action(window, "new")->trigger();
        QVERIFY(window.document().components.empty());
        QVERIFY(schematic->componentRect(0).isNull());
        QCOMPARE(schematic->horizontalScrollBar()->value(), 0);
        QVERIFY(!window.isModified());
    }

    void synthesisWizards_data()
    {
        QTest::addColumn<bool>("matching");
        QTest::addColumn<int>("topology");
        QTest::addColumn<int>("orientation");
        for (int type = 0; type < 3; ++type)
            QTest::newRow(qPrintable(QString("match-%1").arg(type))) << true << type << 0;
        for (int type = 0; type < 2; ++type)
            for (int first = 0; first < 2; ++first)
                QTest::newRow(qPrintable(QString("filter-%1-%2").arg(type).arg(first)))
                    << false << type << first;
    }

    void synthesisWizards()
    {
        QFETCH(bool, matching);
        QFETCH(int, topology);
        QFETCH(int, orientation);
        MainWindow window;
        QVERIFY(window.openDocument(QString(TEST_DATA_DIR) + "/legacy-example.lls"));
        present(window);
        const auto before = window.document();
        bool previewed = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected synthesis wizard.");
            auto* preview = dialog->findChild<QPlainTextEdit*>("synthesisPreview");
            if (!preview) qFatal("Expected synthesis preview.");
            if (matching) {
                dialog->findChild<QComboBox*>("matchType")->setCurrentIndex(topology);
                dialog->findChild<QSpinBox*>("matchSections")->setValue(2);
                dialog->findChild<QDoubleSpinBox*>("matchStartR")->setValue(5);
                dialog->findChild<QDoubleSpinBox*>("matchStartX")->setValue(-10);
                dialog->findChild<QDoubleSpinBox*>("matchGoalR")->setValue(50);
                dialog->findChild<QDoubleSpinBox*>("matchGoalX")->setValue(5);
                dialog->findChild<QDoubleSpinBox*>("matchFrequency")->setValue(915);
            } else {
                dialog->findChild<QComboBox*>("filterType")->setCurrentIndex(topology);
                dialog->findChild<QComboBox*>("filterFirst")->setCurrentIndex(orientation);
                dialog->findChild<QSpinBox*>("filterOrder")->setValue(4);
                dialog->findChild<QDoubleSpinBox*>("filterImpedance")->setValue(75);
                dialog->findChild<QDoubleSpinBox*>("filterCutoff")->setValue(200);
                dialog->findChild<QDoubleSpinBox*>("filterRipple")->setValue(0.5);
            }
            previewed = preview->toPlainText().contains("Endpoint:")
                && (matching || preview->toPlainText().contains("order 5"));
            QTest::keyClick(dialog, Qt::Key_Return);
            if (dialog->isVisible()) dialog->reject();
        });
        QTest::keyClick(window.findChild<SmithChartWidget*>(), Qt::Key_W,
                        matching ? Qt::NoModifier : Qt::ControlModifier);
        QVERIFY(previewed);
        QVERIFY(window.document() != before);
        QVERIFY(window.isModified());
        const auto generated = window.document();
        if (matching) {
            QCOMPARE(generated.frequencyMHz, 915.0);
            QCOMPARE(generated.startOhms, rf::Complex(5, -10));
            QCOMPARE(generated.goalOhms, rf::Complex(50, 5));
            const auto endpoint = rf::evaluate(generated).nodes.back().ohms();
            QVERIFY(std::abs(endpoint - generated.goalOhms) < 1e-8);
            QCOMPARE(generated.components.size(), topology == 2 ? std::size_t(4) : std::size_t(6));
        } else {
            QCOMPARE(generated.frequencyMHz, 200.0);
            QCOMPARE(generated.referenceOhms, 75.0);
            QCOMPARE(generated.components.size(), std::size_t(5));
            QCOMPARE(generated.components.front().connection,
                     orientation == 0 ? rf::Connection::Shunt : rf::Connection::Series);
        }
        QCOMPARE(window.findChild<QTableWidget*>()->rowCount(), int(generated.components.size()));
        action(window, "undo")->trigger();
        QVERIFY(window.document() == before);
        action(window, "redo")->trigger();
        QVERIFY(window.document() == generated);
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected synthesis wizard.");
            QTest::keyClick(dialog, Qt::Key_Escape);
        });
        action(window, matching ? "matchWizard" : "filterWizard")->trigger();
        QVERIFY(window.document() == generated);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("synthesized.rfs.json");
        QVERIFY(window.saveDocument(path));
        QVERIFY(window.openDocument(path));
        QVERIFY(window.document() == generated);
    }

    void synthesisInvalidPreview()
    {
        MainWindow window;
        present(window);
        const auto before = window.document();
        bool disabled = false;
        bool recovered = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected MatchMe wizard.");
            auto* value = dialog->findChild<QDoubleSpinBox*>("matchStartR");
            auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            auto* preview = dialog->findChild<QPlainTextEdit*>("synthesisPreview");
            value->setValue(0);
            disabled = !ok->isEnabled() && preview->toPlainText().contains("Cannot synthesize:");
            value->setValue(10);
            recovered = ok->isEnabled() && preview->toPlainText().contains("Endpoint:");
            dialog->reject();
        });
        action(window, "matchWizard")->trigger();
        QVERIFY(disabled);
        QVERIFY(recovered);
        QVERIFY(window.document() == before);
        QVERIFY(!window.isModified());
        disabled = recovered = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected filter wizard.");
            auto* cutoff = dialog->findChild<QDoubleSpinBox*>("filterCutoff");
            auto* impedance = dialog->findChild<QDoubleSpinBox*>("filterImpedance");
            auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            auto* preview = dialog->findChild<QPlainTextEdit*>("synthesisPreview");
            cutoff->setValue(1e-9);
            impedance->setValue(1e-9);
            disabled = !ok->isEnabled() && preview->toPlainText().contains("Cannot synthesize:");
            cutoff->setValue(1000);
            impedance->setValue(50);
            recovered = ok->isEnabled() && preview->toPlainText().contains("Endpoint:");
            dialog->reject();
        });
        action(window, "filterWizard")->trigger();
        QVERIFY(disabled);
        QVERIFY(recovered);
        QVERIFY(window.document() == before);
        QVERIFY(!window.isModified());
    }

    void exportLtspiceFromMenu()
    {
        const bool nativeDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restore = qScopeGuard([&] {
            QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled);
        });
        MainWindow window;
        QVERIFY(window.openDocument(QString(TEST_DATA_DIR) + "/legacy-example.lls"));
        present(window);
        const auto before = window.document();
        const auto modified = window.isModified();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected LTspice export dialog.");
            dialog->selectFile(directory.filePath("network"));
            static_cast<QDialog*>(dialog)->accept();
        });
        action(window, "exportLtspice")->trigger();
        QFile file(directory.filePath("network.asc"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), ltspice::schematic(before));
        QVERIFY(window.document() == before);
        QCOMPARE(window.isModified(), modified);
        QTimer::singleShot(0, &window, [] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected LTspice export dialog.");
            dialog->reject();
        });
        action(window, "exportLtspice")->trigger();
        QVERIFY(window.document() == before);
    }

    void acceptComponentWithKeyboard_data()
    {
        QTest::addColumn<int>("shortcut");
        QTest::addColumn<QString>("field");
        QTest::addColumn<int>("acceptKey");
        for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
            for (const auto& entry : std::vector<std::pair<Qt::Key, QString>>{
                     {Qt::Key_C, "capacitance"}, {Qt::Key_L, "inductance"},
                     {Qt::Key_R, "resistance"}, {Qt::Key_T, "length"},
                     {Qt::Key_X, "resistance"}, {Qt::Key_I, "turnsRatio"},
                     {Qt::Key_K, "length"}, {Qt::Key_B, "physicalLength"}}) {
                const auto name = QString("%1-%2-%3").arg(QChar(int(entry.first))).arg(entry.second)
                    .arg(key == Qt::Key_Return ? "return" : "enter");
                QTest::newRow(qPrintable(name)) << int(entry.first) << entry.second << int(key);
            }
        }
    }

    void componentConnectionIsKeyboardReachable()
    {
        MainWindow window;
        present(window);
        QTimer::singleShot(0, &window, [] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected component editor.");
            const auto closeDialog = qScopeGuard([dialog] {
                if (dialog->isVisible()) dialog->reject();
            });
            auto* capacitance = dialog->findChild<QDoubleSpinBox*>("capacitance");
            auto* connection = dialog->findChild<QComboBox*>("componentConnection");
            auto* kind = dialog->findChild<QComboBox*>("componentKind");
            if (!capacitance || !connection) qFatal("Missing capacitor editor controls.");
            const auto highlightedBorder = [](QWidget* control) {
                const auto image = control->grab().toImage();
                return image.pixelColor(qRound(image.devicePixelRatio()),
                                        image.height() / 2)
                    == control->palette().color(QPalette::Highlight);
            };
            QTest::qWait(30);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(capacitance));
            QVERIFY(highlightedBorder(capacitance));
            QTest::keyClick(capacitance, Qt::Key_Backtab);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(connection));
            QVERIFY(highlightedBorder(connection));
            QTest::keyClick(connection, Qt::Key_Backtab);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(kind));
            QVERIFY(highlightedBorder(kind));
            QVERIFY(!highlightedBorder(capacitance));
            QTest::keyClick(kind, Qt::Key_Tab);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(connection));
            QVERIFY(highlightedBorder(connection));
            QVERIFY(!highlightedBorder(kind));
            QTest::keyClick(connection, Qt::Key_Down);
            QCOMPARE(connection->currentData().toInt(), int(rf::Connection::Shunt));
            QTest::keyClick(connection, Qt::Key_Tab);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(capacitance));
            QVERIFY(highlightedBorder(capacitance));
            QTest::keyClick(capacitance, Qt::Key_Backtab);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(connection));
            auto* box = dialog->findChild<QDialogButtonBox*>();
            QTest::keyClick(connection, Qt::Key_Tab);
            QTest::keyClick(capacitance, Qt::Key_Tab);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(kind));
            QVERIFY(highlightedBorder(kind));
            QTest::keyClick(kind, Qt::Key_Backtab);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(capacitance));
            QTest::keyClick(capacitance, Qt::Key_Tab);
            QTest::keyClick(kind, Qt::Key_Down);
            QCOMPARE(kind->currentData().toInt(), int(rf::Kind::Inductor));
            QVERIFY(capacitance->isHidden());
            auto* inductance = dialog->findChild<QDoubleSpinBox*>("inductance");
            QTest::keyClick(kind, Qt::Key_Tab);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(connection));
            QTest::keyClick(connection, Qt::Key_Tab);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(inductance));
            QVERIFY(highlightedBorder(inductance));
            QTest::keyClick(inductance, Qt::Key_Backtab);
            QTest::keyClick(connection, Qt::Key_Backtab);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(kind));
            QTest::keyClick(kind, Qt::Key_Up);
            QCOMPARE(kind->currentData().toInt(), int(rf::Kind::Capacitor));
            QTest::keyClick(kind, Qt::Key_Tab);
            QTest::keyClick(connection, Qt::Key_Tab);
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(capacitance));
            QVERIFY(highlightedBorder(capacitance));
            box->button(QDialogButtonBox::Ok)->click();
        });
        action(window, "addC")->trigger();
        QCOMPARE(window.document().components.size(), std::size_t(1));
        QCOMPARE(window.document().components.front().connection, rf::Connection::Shunt);
    }

    void acceptComponentWithKeyboard()
    {
        QFETCH(int, shortcut);
        QFETCH(QString, field);
        QFETCH(int, acceptKey);
        MainWindow window;
        present(window);
        bool accepted = false;
        bool focusedValue = false;
        bool defaultOk = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected component editor.");
            auto* spin = dialog->findChild<QDoubleSpinBox*>(field);
            if (!spin) qFatal("Missing component value.");
            QTest::qWait(30);
            focusedValue = dialog->focusWidget() == spin;
            defaultOk = dialog->findChild<QDialogButtonBox*>()
                ->button(QDialogButtonBox::Ok)->isDefault();
            QTest::keyClicks(spin, "42.5");
            QTest::keyClick(spin, static_cast<Qt::Key>(acceptKey));
            accepted = dialog->result() == QDialog::Accepted;
            if (!accepted) dialog->reject();
        });
        action(window, qPrintable("add" + QString(QChar(shortcut))))->trigger();
        QVERIFY(focusedValue);
        QVERIFY(defaultOk);
        QVERIFY(accepted);
        QCOMPARE(window.document().components.size(), std::size_t(1));
        const auto& component = window.document().components.front();
        if (field == "capacitance") QCOMPARE(component.capacitancePf, 42.5);
        else if (field == "inductance") QCOMPARE(component.inductanceNh, 42.5);
        else if (field == "resistance") QCOMPARE(component.resistance, 42.5);
        else if (field == "turnsRatio") QCOMPARE(component.turnsRatio, 42.5);
        else if (field == "physicalLength") QCOMPARE(component.physicalLengthMm, 42.5);
        else QCOMPARE(component.lengthDegrees, 42.5);
    }

    void componentKeyboardContract_data()
    {
        QTest::addColumn<int>("acceptKey");
        QTest::addColumn<QString>("control");
        QTest::addColumn<bool>("editing");
        for (auto key : {Qt::Key_Return, Qt::Key_Enter})
            for (const auto& control : {"numberAfterCancel", "closedCombo", "openCombo",
                                        "typePopup", "lengthMode", "velocityMode",
                                        "tuningParameter", "calculator", "cancel"})
                for (bool editing : {false, true}) {
                    const auto name = QString("%1-%2-%3").arg(key).arg(control).arg(editing);
                    QTest::newRow(qPrintable(name)) << int(key) << QString(control) << editing;
                }
    }

    void componentKeyboardContract()
    {
        QFETCH(int, acceptKey);
        QFETCH(QString, control);
        QFETCH(bool, editing);
        MainWindow window;
        present(window);
        if (editing) {
            acceptEditor(window, 10);
            action(window, "addC")->trigger();
        }
        bool accepted = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected component editor");
            const auto cleanup = qScopeGuard([dialog] {
                if (dialog->isVisible()) dialog->reject();
            });
            QTest::qWait(30);
            auto* spin = dialog->findChild<QDoubleSpinBox*>("capacitance");
            auto* connection = dialog->findChild<QComboBox*>("componentConnection");
            auto* box = dialog->findChild<QDialogButtonBox*>();
            QWidget* target = spin;
            if (control == "numberAfterCancel" || control == "cancel") {
                box->button(QDialogButtonBox::Cancel)->setFocus();
                if (control == "numberAfterCancel") {
                    spin->setFocus();
                    spin->selectAll();
                    QTest::keyClicks(spin, "42.5");
                } else {
                    target = box->button(QDialogButtonBox::Cancel);
                }
            } else if (control == "closedCombo" || control == "openCombo" || control == "typePopup"
                       || control == "lengthMode" || control == "velocityMode"
                       || control == "tuningParameter") {
                if (control == "lengthMode" || control == "velocityMode" || control == "tuningParameter") {
                    auto* kind = dialog->findChild<QComboBox*>("componentKind");
                    kind->setCurrentIndex(kind->findData(int(control == "tuningParameter"
                        ? rf::Kind::SeriesRlc : rf::Kind::Coax)));
                }
                auto* combo = control == "typePopup"
                    ? dialog->findChild<QComboBox*>("componentKind")
                    : control == "closedCombo" || control == "openCombo" ? connection
                    : dialog->findChild<QComboBox*>(control);
                if (control == "tuningParameter") combo->setCurrentIndex(0);
                combo->setFocus();
                target = combo;
                if (control != "closedCombo") {
                    combo->showPopup();
                    QTest::qWait(30);
                    target = combo->view();
                }
                QTest::keyClick(target, Qt::Key_Down);
            } else if (control == "calculator") {
                auto* kind = dialog->findChild<QComboBox*>("componentKind");
                kind->setCurrentIndex(kind->findData(int(rf::Kind::Microstrip)));
                target = dialog->findChild<QPushButton*>("calculateWidth");
                target->setFocus();
            }
            QTest::keyClick(target, static_cast<Qt::Key>(acceptKey));
            accepted = dialog->result() == QDialog::Accepted;
        });
        action(window, editing ? "editComponent" : "addC")->trigger();
        QVERIFY(accepted);
        QCOMPARE(window.document().components.size(), std::size_t(1));
        const auto& result = window.document().components.front();
        if (control == "numberAfterCancel") QCOMPARE(result.capacitancePf, 42.5);
        if (control == "closedCombo" || control == "openCombo")
            QCOMPARE(result.connection, rf::Connection::Shunt);
        if (control == "typePopup") QCOMPARE(result.kind, rf::Kind::Inductor);
        if (control == "calculator") QCOMPARE(result.kind, rf::Kind::Microstrip);
        if (control == "lengthMode") QCOMPARE(result.lengthMode, rf::LengthMode::Physical);
        if (control == "velocityMode") QCOMPARE(result.velocityMode, rf::VelocityMode::Permittivity);
        if (control == "tuningParameter") QCOMPARE(result.tune, rf::Tune::Inductance);
    }

    void componentInvalidKeyboardEdit()
    {
        MainWindow window;
        present(window);
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected microstrip editor");
            const auto cleanup = qScopeGuard([dialog] {
                if (dialog->isVisible()) dialog->reject();
            });
            auto* width = dialog->findChild<QDoubleSpinBox*>("stripWidth");
            width->setValue(10000);
            auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            QVERIFY(!ok->isEnabled());
            width->setFocus();
            QTest::keyClick(width, Qt::Key_Return);
            QVERIFY(dialog->isVisible());
            QVERIFY(dialog->findChild<QLabel*>("linePreview")->text().startsWith("Invalid component:"));
            width->selectAll();
            QTest::keyClicks(width, "3");
            QTest::keyClick(width, Qt::Key_Enter);
            QCOMPARE(dialog->result(), int(QDialog::Accepted));
        });
        action(window, "addB")->trigger();
        QCOMPARE(window.document().components.size(), std::size_t(1));
        QCOMPARE(window.document().components.front().stripWidthMm, 3.0);
    }

    void componentTabCycle()
    {
        MainWindow window;
        present(window);
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected component editor");
            const auto cleanup = qScopeGuard([dialog] { dialog->reject(); });
            QTest::qWait(30);
            for (int inputMode : {0, 1}) {
                for (const auto& mode : {QString("lengthMode"), QString("velocityMode")}) {
                    auto* combo = dialog->findChild<QComboBox*>(mode);
                    combo->setCurrentIndex(inputMode);
                }
                auto* kind = dialog->findChild<QComboBox*>("componentKind");
                for (int type = 0; type < kind->count(); ++type) {
                    kind->setCurrentIndex(type);
                    QStringList names;
                    for (auto* widget : dialog->findChildren<QWidget*>())
                        if ((qobject_cast<QComboBox*>(widget) || qobject_cast<QDoubleSpinBox*>(widget))
                            && widget->isVisible() && widget->isEnabled())
                            names << widget->objectName();
                    kind->setFocus();
                    QStringList visited;
                    for (int i = 0; i < names.size(); ++i) {
                        auto* current = dialog->focusWidget();
                        visited << current->objectName();
                        QTest::keyClick(current, Qt::Key_Tab);
                        auto* next = dialog->focusWidget();
                        QTest::keyClick(next, Qt::Key_Backtab);
                        QCOMPARE(dialog->focusWidget(), current);
                        if (auto* combo = qobject_cast<QComboBox*>(current)) {
                            combo->showPopup();
                            QTest::keyClick(combo->view(), Qt::Key_Tab);
                            QCOMPARE(dialog->focusWidget(), next);
                            QTest::keyClick(next, Qt::Key_Backtab);
                            combo->showPopup();
                            QTest::keyClick(combo->view(), Qt::Key_Backtab);
                            QVERIFY(!combo->view()->isVisible());
                            QTest::keyClick(dialog->focusWidget(), Qt::Key_Tab);
                            QCOMPARE(dialog->focusWidget(), current);
                        }
                        QTest::keyClick(current, Qt::Key_Tab);
                    }
                    QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(kind));
                    names.sort();
                    visited.sort();
                    QCOMPARE(visited, names);
                }
            }
        });
        action(window, "addK")->trigger();
    }

    void openExamples()
    {
        const bool nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restoreDialogs = qScopeGuard([=] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
        });
        MainWindow window;
        present(window);
        QTemporaryDir directory;
        const auto openExample = [&](const QString& name) {
            QTimer::singleShot(0, &window, [&] {
                auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                if (!dialog) qFatal("Expected examples file dialog");
                const auto cleanup = qScopeGuard([dialog] {
                    if (dialog->isVisible()) dialog->reject();
                });
                QVERIFY(dialog->directory().exists(name));
                dialog->selectFile(dialog->directory().filePath(name));
                static_cast<QDialog*>(dialog)->accept();
            });
            action(window, "openExamples")->trigger();
        };
        for (const auto& name : {QString("legacy-example.lls"), QString("network.rfs_45Mfilter.json")}) {
            openExample(name);
            QVERIFY(!window.document().components.empty());
            QVERIFY(window.isModified());
            QTimer::singleShot(0, &window, [&] {
                auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                if (!dialog) qFatal("Expected Save As for example");
                dialog->selectFile(directory.filePath(name + ".rfs.json"));
                static_cast<QDialog*>(dialog)->accept();
            });
            action(window, "save")->trigger();
            QVERIFY(QFileInfo::exists(directory.filePath(name + ".rfs.json")));
            QVERIFY(!window.isModified());
        }
        const auto before = window.document();
        QTimer::singleShot(0, &window, [] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected examples file dialog");
            dialog->reject();
        });
        action(window, "openExamples")->trigger();
        QCOMPARE(window.document(), before);
    }

    void exportNetworkImage()
    {
        MainWindow window;
        present(window);
        auto* chart = window.findChild<SmithChartWidget*>();
        auto* table = window.findChild<QTableWidget*>();
        auto* schematic = window.findChild<SchematicWidget*>();
        QVERIFY(chart);
        QVERIFY(table);
        QVERIFY(schematic);
        const auto empty = window.chartImage();
        QVERIFY(!empty.isNull());
        QVERIFY(empty.height() > chart->height() + 100);
        const int reportWidth = empty.width() - 40;
        const int bandHeight = 232;
        QCOMPARE(schematic->reportHeight(reportWidth), bandHeight);
        const int schematicTop = chart->height() + 40;
        const auto emptySchematic = empty.copy(20, schematicTop, reportWidth, bandHeight);
        // The source resistor and load are rendered, even with no components.
        QVERIFY(emptySchematic.pixelColor(100, 32 + 60) != QColor(Qt::white));
        QVERIFY(emptySchematic.pixelColor(165, 32 + 78) != QColor(Qt::white));

        rf::Document document;
        document.title = "Network <with> & special characters";
        document.selected = 0;
        for (int i = 0; i < 60; ++i) {
            auto component = rf::defaultComponent(static_cast<rf::Kind>(10 + i % 6),
                                                  document.frequencyMHz, document.referenceOhms);
            document.components.push_back(component);
        }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto project = directory.filePath("network.rfs.json");
        documents::save(project, document);
        QVERIFY(window.openDocument(project));
        const auto before = window.document();
        const auto image = window.chartImage();
        QVERIFY(!image.isNull());
        QVERIFY(image.height() > empty.height() + 60 * 20);
        QVERIFY(table->verticalScrollBar()->maximum() > 0);
        table->verticalScrollBar()->setValue(table->verticalScrollBar()->maximum());
        const int horizontalPosition = schematic->horizontalScrollBar()->value();
        const int verticalPosition = schematic->verticalScrollBar()->value();
        schematic->horizontalScrollBar()->setValue(0);
        schematic->verticalScrollBar()->setValue(schematic->verticalScrollBar()->maximum());
        QCOMPARE(window.chartImage(), image);
        QCOMPARE(schematic->horizontalScrollBar()->value(), 0);
        QCOMPARE(schematic->verticalScrollBar()->value(), schematic->verticalScrollBar()->maximum());
        schematic->horizontalScrollBar()->setValue(horizontalPosition);
        schematic->verticalScrollBar()->setValue(verticalPosition);
        QVERIFY(window.document() == before);
        QVERIFY(!window.isModified());
        const auto png = directory.filePath("network.png");
        QVERIFY(image.save(png, "PNG"));
        QCOMPARE(QImage(png).convertToFormat(image.format()), image);

        const int schematicHeight = schematic->reportHeight(image.width() - 40);
        QVERIFY(schematicHeight > bandHeight);
        // Every complete row is visible in the report, including both offscreen endpoints.
        const auto firstBand = image.copy(20, schematicTop, reportWidth, bandHeight);
        const int lastTop = schematicTop + schematicHeight - bandHeight;
        const auto lastBand = image.copy(20, lastTop, reportWidth, bandHeight);
        QVERIFY(firstBand != lastBand);

        // The tuning field stays in the detailed table, not the compact schematic.
        document.components.back().tune = document.components.back().tune == rf::Tune::Resistance
            ? rf::Tune::Capacitance : rf::Tune::Resistance;
        documents::save(project, document);
        QVERIFY(window.openDocument(project));
        const auto changed = window.chartImage();
        QCOMPARE(changed.size(), image.size());
        QVERIFY(changed != image);
        QCOMPARE(changed.copy(0, 0, image.width(), schematicTop),
                 image.copy(0, 0, image.width(), schematicTop));
        QCOMPARE(changed.copy(20, schematicTop, reportWidth, bandHeight), firstBand);
        QCOMPARE(changed.copy(20, lastTop, reportWidth, bandHeight), lastBand);
        QVERIFY(changed.copy(0, image.height() - 200, image.width(), 200)
                != image.copy(0, image.height() - 200, image.width(), 200));
        const auto screenshot = qEnvironmentVariable("RFDUDE_EXPORT_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(image.save(screenshot, "PNG"));

        // The first document component appears at the load end in the final band.
        document.components.front().resistance = 75;
        documents::save(project, document);
        QVERIFY(window.openDocument(project));
        const auto changedLoadEnd = window.chartImage();
        QVERIFY(changedLoadEnd.copy(20, lastTop, reportWidth, bandHeight) != lastBand);
    }

    void addTuneSelectDeleteUndo()
    {
        MainWindow window;
        present(window);
        auto* chart = window.findChild<SmithChartWidget*>();
        QVERIFY(chart);
        acceptEditor(window, 10);
        QTest::keyClick(chart, Qt::Key_C);
        QCOMPARE(window.document().components.size(), std::size_t(1));
        QCOMPARE(window.document().components[0].capacitancePf, 10.0);
        QCOMPARE(window.document().selected, 0);
        QVERIFY(window.isModified());
        present(window);
        QTest::keyClick(chart, Qt::Key_Up);
        QCOMPARE(window.document().components[0].capacitancePf, 10.25);
        QTest::keyClick(chart, Qt::Key_Down);
        QVERIFY(std::abs(window.document().components[0].capacitancePf - 9.99375) < 1e-9);
        QTest::keyClick(chart, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(window.document().frequencyMHz, 1025.0);
        QTest::keyClick(chart, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(window.document().referenceOhms, 51.25);
        acceptEditor(window, 20, true);
        QTest::keyClick(chart, Qt::Key_C);
        present(window);
        QCOMPARE(window.document().selected, 1);
        QVERIFY(window.document().components[1].connection == rf::Connection::Shunt);
        QTest::keyClick(chart, Qt::Key_Left);
        QCOMPARE(window.document().selected, 0);
        acceptEditor(window, 15);
        QTest::keyClick(chart, Qt::Key_C);
        present(window);
        QCOMPARE(window.document().selected, 1);
        QCOMPARE(window.document().components[1].capacitancePf, 15.0);
        QCOMPARE(window.document().components[2].capacitancePf, 20.0);
        QTest::keyClick(chart, Qt::Key_Delete);
        QCOMPARE(window.document().components.size(), std::size_t(2));
        action(window, "undo")->trigger();
        QCOMPARE(window.document().components.size(), std::size_t(3));
        action(window, "redo")->trigger();
        QCOMPARE(window.document().components.size(), std::size_t(2));
        acceptEditor(window, 33);
        action(window, "editComponent")->trigger();
        QCOMPARE(window.document().components[0].capacitancePf, 33.0);
    }

    void saveLoadAndSelection()
    {
        MainWindow window;
        QVERIFY(window.openDocument(QString(TEST_DATA_DIR) + "/legacy-example.lls"));
        QVERIFY(window.isModified());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("test.rfs.json");
        QVERIFY(window.saveDocument(path));
        QVERIFY(!window.isModified());
        action(window, "previous")->trigger();
        QVERIFY(!window.isModified());
        const auto before = window.document();
        action(window, "increase_component")->trigger();
        QVERIFY(window.isModified());
        QVERIFY(window.openDocument(path));
        QVERIFY(!window.isModified());
        QCOMPARE(window.document().components, before.components);
        QCOMPARE(window.findChild<QTableWidget*>()->rowCount(), 2);
        action(window, "displayA")->trigger();
        QVERIFY(window.document().admittance);
        action(window, "undo")->trigger();
        QVERIFY(!window.document().admittance);
    }

    void wheelAndChartSelection()
    {
        MainWindow window;
        QVERIFY(window.openDocument(QString(TEST_DATA_DIR) + "/legacy-example.lls"));
        present(window);
        auto* chart = window.findChild<SmithChartWidget*>();
        QVERIFY(chart);
        const auto evaluation = rf::evaluate(window.document());
        const auto midpoint = chart->pointForReflection(evaluation.trajectories[0][128]);
        QTest::mouseClick(chart, Qt::LeftButton, Qt::NoModifier, midpoint.toPoint());
        QCOMPARE(window.document().selected, 0);
        const auto before = window.document().components[0].inductanceNh;
        QWheelEvent wheel(midpoint, chart->mapToGlobal(midpoint.toPoint()), {}, {0, 240},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(chart, &wheel);
        QVERIFY(std::abs(window.document().components[0].inductanceNh - before * 1.025 * 1.025) < 1e-9);
        QWheelEvent frequency(midpoint, chart->mapToGlobal(midpoint.toPoint()), {}, {0, -120},
                              Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(chart, &frequency);
        QCOMPARE(window.document().frequencyMHz, 1170.0);
        const auto point = chart->pointForReflection({0.2, -0.3});
        QVERIFY(std::abs(chart->reflectionAt(point) - rf::Complex(0.2, -0.3)) < 1e-12);
    }

    void setupAndPresets()
    {
        MainWindow window;
        present(window);
        QTimer::singleShot(0, &window, [] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected setup dialog.");
            dialog->findChild<QDoubleSpinBox*>("frequency")->setValue(915);
            dialog->findChild<QDoubleSpinBox*>("preset1")->setValue(433);
            dialog->findChild<QDoubleSpinBox*>("startX")->setValue(-25);
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        action(window, "setup")->trigger();
        QCOMPARE(window.document().frequencyMHz, 915.0);
        QCOMPARE(window.document().startOhms.imag(), -25.0);
        action(window, "preset1")->trigger();
        QCOMPARE(window.document().frequencyMHz, 433.0);
    }

    void failedIoPreservesNetwork()
    {
        MainWindow window;
        QVERIFY(window.openDocument(QString(TEST_DATA_DIR) + "/legacy-example.lls"));
        const auto before = window.document();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto dismissError = [&] {
            QTimer::singleShot(0, &window, [] {
                auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (!box) qFatal("Expected error message.");
                box->accept();
            });
        };
        dismissError();
        QVERIFY(!window.openDocument(directory.filePath("missing.rfs.json")));
        QVERIFY(window.document() == before);
        dismissError();
        QVERIFY(!window.saveDocument(directory.filePath("missing-directory/network.rfs.json")));
        QVERIFY(window.document() == before);
        QVERIFY(window.isModified());
        dismissError();
        QVERIFY(!window.saveDocument(QString(TEST_DATA_DIR) + "/legacy-example.lls"));
        QVERIFY(window.document() == before);
        QVERIFY(window.saveDocument(directory.filePath("good.rfs.json")));
        QVERIFY(!window.isModified());
    }

    void cancelEditorAndClose()
    {
        MainWindow window;
        present(window);
        QTimer::singleShot(0, &window, [] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected component dialog.");
            QTest::keyClick(dialog, Qt::Key_Escape);
            QVERIFY(dialog->isVisible());
            auto* cancel = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel);
            QCOMPARE(cancel->focusPolicy(), Qt::NoFocus);
            QTest::mouseClick(cancel, Qt::LeftButton);
        });
        action(window, "addC")->trigger();
        QVERIFY(window.document().components.empty());
        QVERIFY(!window.isModified());
        action(window, "displayA")->trigger();
        QTimer::singleShot(0, &window, [] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) qFatal("Expected unsaved changes prompt.");
            box->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        QTimer::singleShot(0, &window, [] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) qFatal("Expected unsaved changes prompt.");
            box->button(QMessageBox::Discard)->click();
        });
        QVERIFY(window.close());
    }

    void renderChart()
    {
        MainWindow window;
        QVERIFY(window.openDocument(QString(TEST_DATA_DIR) + "/legacy-example.lls"));
        action(window, "displayA")->trigger();
        action(window, "displayV")->trigger();
        action(window, "displayQ")->trigger();
        present(window);
        auto* chart = window.findChild<SmithChartWidget*>();
        const auto image = chart->grab().toImage();
        QVERIFY(!image.isNull());
        int nonwhite = 0;
        int blue = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const auto color = image.pixelColor(x, y);
                if (color.red() < 240 || color.green() < 240 || color.blue() < 240) ++nonwhite;
                if (color.blue() > color.red() + 30 && color.blue() > color.green() + 30) ++blue;
            }
        }
        QVERIFY(nonwhite > 1000);
        QVERIFY(blue > 100);
        QVERIFY(window.findChild<QLabel*>("networkMetrics")->text().contains("VSWR"));
        const auto screenshot = qEnvironmentVariable("RFDUDE_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(window.grab().save(screenshot));
    }
};

QTEST_MAIN(UiTests)
#include "UiTests.moc"
