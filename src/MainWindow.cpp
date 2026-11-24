#include "MainWindow.h"

#include "DocumentIO.h"
#include "ComponentPalette.h"
#include "Editors.h"
#include "FrequencyResponseDialog.h"
#include "LtspiceExport.h"
#include "SmithChartWidget.h"
#include "SchematicWidget.h"
#include "SynthesisDialogs.h"
#include "Touchstone.h"
#include "UpdateCheck.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QClipboard>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDesktopServices>
#include <QUrl>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextDocument>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#ifndef RFDUDE_BUILD_TAG
#define RFDUDE_BUILD_TAG "local"
#endif

namespace {
QString number(double value)
{
    return std::isfinite(value) ? QString::number(value, 'g', 7) : QStringLiteral("infinite");
}

constexpr auto componentMimeType = "application/vnd.rfdude.component+json";
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setObjectName("mainWindow");
    resize(1250, 980);
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    summary_ = new QLabel;
    summary_->setObjectName("networkSummary");
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary_);
    auto* splitter = new QSplitter;
    table_ = new QTableWidget(0, 4);
    table_->setObjectName("componentTable");
    table_->setHorizontalHeaderLabels({"#", "Connection", "Component", "Resulting impedance"});
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setMinimumWidth(300);
    splitter->addWidget(table_);
    chart_ = new SmithChartWidget;
    splitter->addWidget(chart_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({440, 760});
    auto* workspace = new QSplitter(Qt::Vertical);
    workspace->setObjectName("workspaceSplitter");
    workspace->addWidget(splitter);
    schematic_ = new SchematicWidget;
    workspace->addWidget(schematic_);
    workspace->setStretchFactor(0, 3);
    workspace->setStretchFactor(1, 1);
    workspace->setSizes({520, 220});
    layout->addWidget(workspace, 1);
    metrics_ = new QLabel;
    metrics_->setObjectName("networkMetrics");
    metrics_->setWordWrap(true);
    metrics_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(metrics_);
    auto* hints = new QLabel(tr("C/L/R/T/X/I/K/B: add  |  Ctrl+I: insert right  |  Ctrl+C/X/V: copy/cut/paste  |  "
        "Left/Right: select  |  Up/Down or wheel: tune  |  Ctrl+tune: frequency  |  Shift+tune: Z0  |  E: edit  |  S: setup"));
    hints->setWordWrap(true);
    layout->addWidget(hints);
    setCentralWidget(central);
    buildActions();
    table_->installEventFilter(this);
    table_->viewport()->installEventFilter(this);
    chart_->installEventFilter(this);
    schematic_->installEventFilter(this);
    schematic_->viewport()->installEventFilter(this);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        const int row = table_->rowAt(position.y());
        if (row < 0 || row >= table_->rowCount()) return;
        table_->selectRow(row);
        select(row);
        QMenu menu(table_);
        menu.addAction(insertAction_);
        menu.addSeparator();
        menu.addAction(cutAction_);
        menu.addAction(copyAction_);
        menu.addAction(pasteAction_);
        menu.addSeparator();
        menu.addAction(editAction_);
        menu.addAction(deleteAction_);
        menu.exec(table_->viewport()->mapToGlobal(position));
    });
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (row >= 0) select(row);
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this] { editSelected(); });
    connect(chart_, &SmithChartWidget::componentSelected, this, &MainWindow::select);
    connect(chart_, &SmithChartWidget::editRequested, this, &MainWindow::editSelected);

    connect(schematic_, &SchematicWidget::componentSelected, this, &MainWindow::select);
    connect(schematic_, &SchematicWidget::editRequested, this, &MainWindow::editSelected);
    connect(schematic_, &SchematicWidget::contextMenuRequested, this,
            [this](int index, QPoint globalPosition) {
        if (index < 0 || index >= static_cast<int>(document_.components.size())) return;
        select(index);
        QMenu menu(schematic_);
        menu.addAction(insertAction_);
        menu.addSeparator();
        menu.addAction(cutAction_);
        menu.addAction(copyAction_);
        menu.addAction(pasteAction_);
        menu.addSeparator();
        menu.addAction(editAction_);
        menu.addAction(deleteAction_);
        menu.exec(globalPosition);
    });
    // Clicking an end of the schematic opens the setup with the matching impedance focused.
    connect(schematic_, &SchematicWidget::sourceClicked, this, [this] { settings("goalR"); });
    connect(schematic_, &SchematicWidget::loadClicked, this, [this] { settings("startR"); });

    connect(chart_, &SmithChartWidget::tuneRequested, this, &MainWindow::tune);
    connect(chart_, &SmithChartWidget::impedanceRequested, this, &MainWindow::inspectImpedance);
    connect(chart_, &SmithChartWidget::hoverText, this, [this](const QString& text) {
        statusBar()->showMessage(text);
    });
    evaluation_ = rf::evaluate(document_);
    refresh();
    chart_->setFocus();
}

void MainWindow::checkForUpdates()
{
    if (!updateNetwork_) updateNetwork_ = new QNetworkAccessManager(this);
    QNetworkRequest request(
        QUrl("https://api.github.com/repos/llascari/RFdudeSmithChartV2/releases?per_page=20"));
    request.setHeader(QNetworkRequest::UserAgentHeader, "RFdudeSmithChartV2");
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(12000);
    QNetworkReply* reply = updateNetwork_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Update check failed"),
                tr("Could not check for updates:\n%1").arg(reply->errorString()));
            return;
        }
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            QMessageBox::warning(this, tr("Update check failed"),
                tr("GitHub returned HTTP %1 while checking for updates.").arg(status));
            return;
        }
        QJsonParseError parseError;
        const auto json = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !json.isArray()) {
            QMessageBox::warning(this, tr("Update check failed"),
                tr("GitHub returned an invalid update response."));
            return;
        }
        struct NewerRelease
        {
            QString tag;
            QString name;
            QString url;
            bool prerelease = false;
        };
        std::vector<NewerRelease> newerReleases;
        NewerRelease latestRelease;
        const QString current = QCoreApplication::applicationVersion();
        const QString currentBuildTag = QString::fromLatin1(RFDUDE_BUILD_TAG);
        const auto currentVersion = rf::parseReleaseVersion(current.toStdString());
        for (const auto& value : json.array()) {
            if (!value.isObject()) continue;
            const auto candidate = value.toObject();
            if (candidate.value("draft").toBool()) continue;
            const QString candidateTag = candidate.value("tag_name").toString();
            const QString candidateName = candidate.value("name").toString();
            const QString candidateUrl = candidate.value("html_url").toString();
            if (!candidateUrl.startsWith("https://github.com/llascari/RFdudeSmithChartV2/releases/")) continue;
            const bool candidatePrerelease = candidate.value("prerelease").toBool();
            const auto candidateVersion = rf::parseReleaseVersionInText(
                !candidateName.isEmpty()
                    ? candidateName.toStdString()
                    : candidateTag.toStdString());
            if (!candidateVersion) continue;
            if (latestRelease.tag.isEmpty()) {
                latestRelease = {candidateTag, candidateName, candidateUrl, candidatePrerelease};
            }
            if (!currentVersion) continue;

            bool newer = candidateVersion->major > currentVersion->major
                || (candidateVersion->major == currentVersion->major
                    && candidateVersion->minor > currentVersion->minor)
                || (candidateVersion->major == currentVersion->major
                    && candidateVersion->minor == currentVersion->minor
                    && candidateVersion->patch > currentVersion->patch);
            if (!newer
                && candidateVersion->major == currentVersion->major
                && candidateVersion->minor == currentVersion->minor
                && candidateVersion->patch == currentVersion->patch) {
                if (rf::buildTagIsNewer(candidateTag.toStdString(), currentBuildTag.toStdString())) {
                    newer = true;
                } else if (candidatePrerelease
                           && rf::parseBuildTag(candidateTag.toStdString())
                           && !rf::parseBuildTag(currentBuildTag.toStdString())) {
                    newer = true;
                }
            }
            if (newer)
                newerReleases.push_back({candidateTag, candidateName, candidateUrl, candidatePrerelease});
        }
        if (latestRelease.tag.isEmpty() || latestRelease.url.isEmpty()) {
            QMessageBox::warning(this, tr("Update check failed"),
                tr("GitHub returned incomplete release information."));
            return;
        }
        if (newerReleases.empty()) {
            const QString latestLabel = latestRelease.name.isEmpty()
                ? latestRelease.tag
                : latestRelease.name;
            QMessageBox::information(this, tr("No update available"),
                tr("RFdude Smith Chart V2 %1 is current.\nBuild: %2\n\nLatest build/release: %3")
                    .arg(current, currentBuildTag, latestLabel));
            return;
        }
        QStringList newerLabels;
        for (std::size_t i = 0; i < newerReleases.size() && i < 10; ++i) {
            const auto& release = newerReleases[i];
            const QString label = release.name.isEmpty() ? release.tag : release.name;
            newerLabels.append(release.prerelease
                ? tr("%1 (pre-release)").arg(label)
                : label);
        }
        if (newerReleases.size() > 10)
            newerLabels.append(tr("... and %1 more").arg(newerReleases.size() - 10));
        QMessageBox box(QMessageBox::Information, tr("Update available"),
            tr("Newer RFdude Smith Chart V2 build(s) are available.\n\n"
               "Installed: %1\nBuild: %2\n\nNewer builds:\n%3")
                .arg(current, currentBuildTag, newerLabels.join("\n")),
            QMessageBox::Cancel, this);
        QPushButton* openButton = box.addButton(tr("Open Releases page"), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() == openButton)
            QDesktopServices::openUrl(
                QUrl("https://github.com/llascari/RFdudeSmithChartV2/releases"));
    });
}

void MainWindow::buildActions()
{
    auto* file = menuBar()->addMenu(tr("&File"));
    auto* edit = menuBar()->addMenu(tr("&Edit"));
    auto* add = menuBar()->addMenu(tr("&Add"));
    auto* view = menuBar()->addMenu(tr("&View"));
    auto* network = menuBar()->addMenu(tr("&Network"));
    auto* help = menuBar()->addMenu(tr("&Help"));

    // One toolbar per group, each with a bold caption, in workflow order.
    const auto makeGroup = [this](const QString& caption, const QString& objectName) {
        auto* bar = addToolBar(caption);
        bar->setObjectName(objectName);
        bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
        auto* label = new QLabel(caption);
        label->setStyleSheet("QLabel { font-weight: bold; padding: 0 6px; }");
        bar->addWidget(label);
        return bar;
    };
    auto* networkBar = makeGroup(tr("Network"), "networkToolbar");
    auto* selectedBar = makeGroup(tr("Selected"), "selectedToolbar");
    auto* synthesisBar = makeGroup(tr("Synthesis"), "synthesisToolbar");
    addToolBarBreak(); // New line. The components' ribbon is quite large
    auto* componentBar = makeGroup(tr("Add component"), "componentToolbar");

    // Toolbar buttons use the short iconText; menus keep the full text.
    const auto describe = [](QAction* a, const QString& shortText, const QString& tip) {
        a->setIconText(shortText);
        a->setToolTip(tip);
    };

    const auto action = [this](QMenu* menu, const QString& label, const QKeySequence& shortcut,
                                const QString& name, const std::function<void()>& callback) {
        auto* a = menu->addAction(label);
        a->setObjectName(name);
        if (!shortcut.isEmpty()) a->setShortcut(shortcut);
        connect(a, &QAction::triggered, this, callback);
        return a;
    };
    action(file, tr("&New"), QKeySequence::New, "new", [this] { newDocument(); });
    action(file, tr("&Open / import..."), QKeySequence::Open, "open", [this] { chooseOpen(); });
    action(file, tr("Open examples..."), {}, "openExamples", [this] { chooseExample(); });
    action(file, tr("Import Touchstone..."), {}, "importTouchstone", [this] { chooseTouchstone(); });
    action(file, tr("Import reference Touchstone overlay..."), {}, "importReferenceOverlay",
           [this] { chooseReferenceOverlay(); });
    action(file, tr("&Save"), QKeySequence::Save, "save", [this] { chooseSave(); });
    action(file, tr("Save &as..."), QKeySequence::SaveAs, "saveAs", [this] { chooseSave(true); });
    file->addSeparator();
    action(file, tr("Export chart / schematic / response PNG..."), {}, "exportChart", [this] { exportChart(); });
    action(file, tr("Export LTspice schematic..."), {}, "exportLtspice", [this] { exportLtspice(); });
    action(file, tr("&Quit"), QKeySequence::Quit, "quit", [this] { close(); });
    undoAction_ = action(edit, tr("&Undo"), QKeySequence::Undo, "undo", [this] { undo(); });
    redoAction_ = action(edit, tr("&Redo"), QKeySequence::Redo, "redo", [this] { redo(); });
    edit->addSeparator();
    cutAction_ = action(edit, tr("Cu&t component"), QKeySequence::Cut, "cutComponent",
                        [this] { cutSelected(); });
    copyAction_ = action(edit, tr("&Copy component"), QKeySequence::Copy, "copyComponent",
                         [this] { copySelected(); });
    pasteAction_ = action(edit, tr("&Paste component"), QKeySequence::Paste, "pasteComponent",
                          [this] { pasteComponent(); });
    insertAction_ = action(edit, tr("Insert component to the &right..."), QKeySequence("Ctrl+I"),
                           "insertComponent", [this] { insertComponentRight(); });
    edit->addSeparator();
    editAction_ = action(edit, tr("&Edit component..."), QKeySequence("E"), "editComponent", [this] { editSelected(); });
    deleteAction_ = action(edit, tr("&Delete component"), QKeySequence(Qt::Key_Delete), "deleteComponent", [this] { deleteSelected(); });
    auto* setup = action(network, tr("&Setup..."), QKeySequence("S"), "setup", [this] { settings("startR"); });

    describe(setup, tr("Setup"), tr("Network setup: frequency, Z0, start/goal impedance (S)"));
    networkBar->addAction(setup);

    auto* match = action(network, tr("MatchMe wizard..."), QKeySequence("W"), "matchWizard", [this] {
        const auto value = matchWizard(this, document_);
        if (value) change([&](rf::Document& d) { d = *value; });
    });
    describe(match, tr("MatchMe"), tr("MatchMe wizard (W)"));
    synthesisBar->addAction(match);

    auto* filter = action(network, tr("Simple filter wizard..."), QKeySequence("Ctrl+W"), "filterWizard", [this] {
        const auto value = filterWizard(this, document_);
        if (value) {
        change([&](rf::Document& d) {
            d = *value;
            d.frequencyResponseConfigured = true;
            d.frequencyResponseTerminated = false;
        });
            // A filter is judged by its transmission, so show the two-port response.
            twoPortResponse_ = true;
            if (response_) response_->setTerminated(false);
        }
    });
    describe(filter, tr("Filter"), tr("Simple filter wizard (Ctrl+W)"));
    synthesisBar->addAction(filter);

    auto* response = action(network, tr("Frequency response..."), QKeySequence("Ctrl+J"),
                            "frequencyResponse", [this] { showFrequencyResponse(); });
    describe(response, tr("Frequency response"), tr("Show the frequency response panel (Ctrl+J)"));
    networkBar->addAction(response);

    action(view, tr("Frequency response..."), {}, "viewFrequencyResponse",
           [this] { showFrequencyResponse(); });
    action(network, tr("Microstrip calculator / add element..."), {}, "microstripCalculator",
           [this] { addComponent(rf::Kind::Microstrip); });
    action(network, tr("Import Touchstone block / load..."), {}, "networkTouchstone",
           [this] { chooseTouchstone(); });
    action(network, tr("Use constant starting load"), {}, "clearMeasuredLoad", [this] {
        change([](rf::Document& d) { d.measuredLoad.reset(); });
    });
    action(network, tr("Clear reference Touchstone overlay"), {}, "clearReferenceOverlay",
           [this] { clearReferenceOverlay(); });
    for (const auto& entry : std::vector<std::pair<rf::Kind, QString>>{
            {rf::Kind::Capacitor, "C"}, {rf::Kind::Inductor, "L"}, {rf::Kind::Resistor, "R"},
            {rf::Kind::Line, "T"}, {rf::Kind::SeriesRlc, "X"},
            {rf::Kind::Transformer, "I"}, {rf::Kind::Coax, "K"}, {rf::Kind::Microstrip, "B"}}) {
        auto* a = action(add, QString::fromStdString(rf::kindName(entry.first)) + "...",
            QKeySequence(entry.second), "add" + entry.second, [this, kind = entry.first] { addComponent(kind); });
        const auto name = QString::fromStdString(rf::kindName(entry.first));
        describe(a, name, tr("Add %1 (%2)").arg(name.toLower(), entry.second));
    }

    auto* palette = new ComponentPalette;
    componentBar->addWidget(palette);
    connect(palette, &ComponentPalette::addRequested, this,
            [this](rf::Kind kind, rf::Connection connection) { addComponent(kind, connection); });

    describe(insertAction_, tr("Insert right"), tr("Insert a new component to the right (Ctrl+I)"));
    describe(cutAction_, tr("Cut"), tr("Cut selected component (Ctrl+X)"));
    describe(copyAction_, tr("Copy"), tr("Copy selected component (Ctrl+C)"));
    describe(pasteAction_, tr("Paste"), tr("Paste component to the right (Ctrl+V)"));
    describe(editAction_, tr("Edit"), tr("Edit selected component (E)"));
    describe(deleteAction_, tr("Delete"), tr("Delete selected component (Del)"));
    selectedBar->addAction(insertAction_);
    selectedBar->addAction(cutAction_);
    selectedBar->addAction(copyAction_);
    selectedBar->addAction(pasteAction_);
    selectedBar->addAction(editAction_);
    selectedBar->addAction(deleteAction_);

    const std::vector<std::pair<QString, QString>> displays{
        {"Admittance grid", "A"}, {"Magnitude / angle", "G"}, {"Sampled maximum-Q curves", "Q"},
        {"Point labels", "M"}, {"VSWR circle (chart Z0)", "V"}};
    for (std::size_t i = 0; i < displays.size(); ++i) {
        auto* a = action(view, displays[i].first, QKeySequence(displays[i].second),
            "display" + displays[i].second, [this, i] {
                change([i](rf::Document& d) {
                    const std::array<bool*, 5> flags{&d.admittance, &d.polar, &d.qCurves, &d.markers, &d.vswrCircle};
                    *flags[i] = !*flags[i];
                });
            });
        a->setCheckable(true);
        displayActions_.push_back(a);
    }
    s11TrajectoryAction_ = action(view, tr("S11 frequency trajectory"), {},
        "s11Trajectory", [this] {
            change([](rf::Document& d) { d.s11Trajectory = !d.s11Trajectory; });
        });
    s11TrajectoryAction_->setCheckable(true);
    s22TrajectoryAction_ = action(view, tr("S22 frequency trajectory"), {},
        "s22Trajectory", [this] {
            change([](rf::Document& d) { d.s22Trajectory = !d.s22Trajectory; });
        });
    s22TrajectoryAction_->setCheckable(true);
    simpleTrajectoriesAction_ = action(view, tr("Simple 3-frequency Smith-chart trajectories"), {},
        "simpleTrajectories", [this] {
            change([](rf::Document& d) { d.simpleTrajectories = !d.simpleTrajectories; });
        });
    simpleTrajectoriesAction_->setCheckable(true);
    stabilitySourceCircleAction_ = action(view, tr("Source stability circle"), {},
        "stabilitySourceCircle", [this] {
            change([](rf::Document& d) { d.stabilitySourceCircle = !d.stabilitySourceCircle; });
        });
    stabilitySourceCircleAction_->setCheckable(true);
    stabilityLoadCircleAction_ = action(view, tr("Load stability circle"), {},
        "stabilityLoadCircle", [this] {
            change([](rf::Document& d) { d.stabilityLoadCircle = !d.stabilityLoadCircle; });
        });
    stabilityLoadCircleAction_->setCheckable(true);
    stabilityUnstableShadeAction_ = action(view, tr("Shade unstable stability circles"), {},
        "stabilityUnstableShade", [this] {
            change([](rf::Document& d) { d.stabilityUnstableShade = !d.stabilityUnstableShade; });
        });
    stabilityUnstableShadeAction_->setCheckable(true);
    referenceOverlayAction_ = action(view, tr("Reference Touchstone overlay"), {},
        "referenceOverlay", [this] {
            change([](rf::Document& d) {
                d.referenceOverlayVisible =
                    d.referenceOverlay.has_value() && !d.referenceOverlayVisible;
            });
        });
    referenceOverlayAction_->setCheckable(true);
    action(network, tr("Frequency..."), QKeySequence("F"), "frequency", [this] { settings("frequency"); });
    action(network, tr("Reference Z0..."), QKeySequence("Z"), "reference", [this] { settings("z0"); });
    action(network, tr("Tuning percentage..."), QKeySequence("P"), "tuneStep", [this] { settings("step"); });
    action(network, tr("Previous component"), QKeySequence(Qt::Key_Left), "previous", [this] {
        if (!document_.components.empty())
            select((document_.selected + static_cast<int>(document_.components.size()) - 1)
                % static_cast<int>(document_.components.size()));
    });
    action(network, tr("Next component"), QKeySequence(Qt::Key_Right), "next", [this] {
        if (!document_.components.empty())
            select((document_.selected + 1) % static_cast<int>(document_.components.size()));
    });
    for (const bool increase : {true, false}) {
        for (const auto modifier : {Qt::NoModifier, Qt::ControlModifier, Qt::ShiftModifier}) {
            const int key = increase ? Qt::Key_Up : Qt::Key_Down;
            const QString target = modifier == Qt::ControlModifier ? "frequency"
                : modifier == Qt::ShiftModifier ? "Z0" : "component";
            action(network, (increase ? tr("Increase ") : tr("Decrease ")) + target,
                QKeySequence(QKeyCombination(modifier, static_cast<Qt::Key>(key))),
                (increase ? "increase_" : "decrease_") + target,
                [this, increase, modifier] { tune(increase ? 1 : -1, modifier); });
        }
    }
    for (int i = 0; i < 3; ++i) {
        action(network, tr("Frequency preset %1").arg(i + 1), QKeySequence(Qt::Key_F1 + i),
            QString("preset%1").arg(i + 1), [this, i] {
                change([i](rf::Document& d) {
                    d.frequencyMHz = rf::clampMeasuredFrequency(
                        d, d.frequencyPresets[static_cast<std::size_t>(i)]);
                });
            });
    }
    action(help, tr("Check for updates..."), {}, "checkForUpdates",
           [this] { checkForUpdates(); });
    action(help, tr("About"), {}, "about", [this] {
        QMessageBox box(QMessageBox::Information, tr("RFdude Smith Chart V2"),
            tr("<p><b>RFdude Smith Chart V2 %1</b>"
               " - first usable prototype</p>"
               "<p>Build: %2</p>"
               "<p>Ideal R/L/C, transformers, lossless lines/coax, and dielectric-loss microstrip."
               "<br>Project files are versioned JSON; legacy .lls files are import-only."
               "<br>VSWR/return loss refer to real chart Z0, not insertion loss.</p>"
               "<p>Lance Lascari created the original RFdude Smith Chart "
               "application and directed this new V2 project."
               "<br>GitHub Copilot was the principal implementation assistant "
               "for this phase and performed most of the implementation work, "
               "including substantial code, testing, and documentation "
               "contributions, under Lance's direction and review."
               "<br>Original V2 code and supplied legacy .lls examples are "
               "licensed under MIT; original application source and third-party "
               "materials are not included."
               "<br>Pull requests are not being solicited or accepted."
               "<br>The original RFdude Smith Chart application is used as a behavioral "
               "reference with the author's permission.</p>"
               "<p><b>Credits and external references</b><br>"
               "Built with Qt %3, copyright The Qt Company Ltd. and contributors "
               "(<a href=\"https://www.qt.io/\">Qt</a>; dynamically linked).<br>"
               "Microstrip model: Hammerstad and Jensen, "
               "<i>Accurate Models for Microstrip Computer-Aided Design</i>, 1980 "
               "(<a href=\"https://doi.org/10.1109/MWSYM.1980.1124303\">DOI</a>); "
               "equation reference: <a href=\"https://qucs.sourceforge.net/tech/node75.html\">"
               "Qucs Technical Papers</a>.<br>"
               "Attenuator equations: <a href=\"https://qucs.sourceforge.net/tech/node95.html\">"
               "Qucs Technical Papers</a>.<br>"
               "S-parameter reference conversion: Pozar, <i>Microwave Engineering</i>, "
               "4th ed., section 4.3.<br>"
               "Touchstone format: <a href=\"https://ibis.org/touchstone_ver2.0/touchstone_ver2_0.pdf\">"
               "IBIS Open Forum specification</a>.</p>")
               .arg(QCoreApplication::applicationVersion(),
                    QString::fromLatin1(RFDUDE_BUILD_TAG),
                    QString::fromLatin1(qVersion())),
            QMessageBox::Ok, this);
        box.setTextFormat(Qt::RichText);
        box.exec();
    });
}

bool MainWindow::isModified() const
{
    auto current = document_;
    auto saved = saved_;
    current.selected = saved.selected = -1;
    return imported_ || current != saved;
}

void MainWindow::refresh()
{
    setWindowTitle(QString("%1[*] - RFdude Smith Chart V2").arg(QString::fromStdString(document_.title)));
    setWindowModified(isModified());
    summary_->setText(tr("%1\nFrequency: %2 MHz     Z0: %3 ohm     Tune: %4%\nStart: %5     Goal: %6")
        .arg(QString::fromStdString(document_.title), number(document_.frequencyMHz),
             number(document_.referenceOhms), number(document_.tunePercent),
             impedanceText(rf::startingImpedance(document_), document_.polar)
                 + (document_.measuredLoad ? tr(" [%1]").arg(QString::fromStdString(document_.measuredLoad->name)) : QString{}),
             impedanceText(rf::Impedance::fromOhms(document_.goalOhms), document_.polar)));
    {
        const QSignalBlocker blocker(table_);
        table_->setRowCount(static_cast<int>(document_.components.size()));
        for (int row = 0; row < table_->rowCount(); ++row) {
            const auto& c = document_.components[static_cast<std::size_t>(row)];
            const QStringList values{QString::number(row + 1),
                QString::fromStdString(rf::connectionName(c.connection)), componentDescription(c),
                impedanceText(evaluation_.nodes[static_cast<std::size_t>(row + 1)], document_.polar)};
            for (int column = 0; column < 4; ++column) {
                auto* item = table_->item(row, column);
                if (!item) {
                    item = new QTableWidgetItem(values[column]);
                    item->setToolTip(values[column]);
                    table_->setItem(row, column, item);
                } else if (item->text() != values[column]) {
                    item->setText(values[column]);
                    item->setToolTip(values[column]);
                }
            }
        }
        if (document_.selected >= 0) table_->selectRow(document_.selected);
    }
    chart_->setNetwork(document_, evaluation_);
    schematic_->setNetwork(document_);
    if (response_ && response_->isVisible()) response_->setNetwork(document_);
    const auto gamma = evaluation_.nodes.back().reflection(document_.referenceOhms);
    const double magnitude = std::abs(gamma);
    const double swr = magnitude >= 1 - 1e-12 ? INFINITY : (1 + magnitude) / (1 - magnitude);
    const double rl = magnitude < 1e-15 ? INFINITY : -20 * std::log10(magnitude);
    const double loss = magnitude >= 1 ? INFINITY : -10 * std::log10(1 - magnitude * magnitude);
    metrics_->setText(tr("End: %1\n|Gamma|: %2    VSWR (Z0): %3    Return loss: %4 dB    "
        "Mismatch loss: %5 dB    Sampled max |X|/R: %6\n"
        "Ideal elements except microstrip dielectric loss and measured blocks; mismatch loss is not insertion loss.")
        .arg(impedanceText(evaluation_.nodes.back(), document_.polar), number(magnitude),
             magnitude > 1 ? tr("undefined (active)") : number(swr), number(rl),
             magnitude > 1 ? tr("undefined (active)") : number(loss),
             evaluation_.qDefined ? number(evaluation_.maxQ) : tr("undefined (active)")));
    undoAction_->setEnabled(!undo_.empty());
    redoAction_->setEnabled(!redo_.empty());
    editAction_->setEnabled(document_.selected >= 0);
    deleteAction_->setEnabled(document_.selected >= 0);
    cutAction_->setEnabled(document_.selected >= 0);
    copyAction_->setEnabled(document_.selected >= 0);
    pasteAction_->setEnabled(document_.components.size() < 1000
                              && readClipboardComponent().has_value());
    insertAction_->setEnabled(document_.components.size() < 1000);
    const std::array<bool, 5> flags{document_.admittance, document_.polar, document_.qCurves,
                                  document_.markers, document_.vswrCircle};
    for (std::size_t i = 0; i < flags.size(); ++i) displayActions_[i]->setChecked(flags[i]);
    s11TrajectoryAction_->setChecked(document_.s11Trajectory);
    s22TrajectoryAction_->setChecked(document_.s22Trajectory);
    simpleTrajectoriesAction_->setChecked(document_.simpleTrajectories);
    stabilitySourceCircleAction_->setChecked(document_.stabilitySourceCircle);
    stabilityLoadCircleAction_->setChecked(document_.stabilityLoadCircle);
    stabilityUnstableShadeAction_->setChecked(document_.stabilityUnstableShade);
    referenceOverlayAction_->setChecked(document_.referenceOverlay.has_value()
                                       && document_.referenceOverlayVisible);
}

void MainWindow::ensureFrequencyResponse()
{
    if (!response_) {
        // The response view is a dock panel so it can sit next to the Smith chart and
        // update live while the network is edited. It can still be floated or moved.
        response_ = new FrequencyResponseDialog(document_);
        responseDock_ = new QDockWidget(tr("Frequency response"), this);
        responseDock_->setObjectName("frequencyResponseDock");
        responseDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
        responseDock_->setWidget(response_);
        addDockWidget(Qt::RightDockWidgetArea, responseDock_);
        resizeDocks({responseDock_}, {820}, Qt::Horizontal);
        responseDock_->hide();
        connect(response_, &QDialog::rejected, responseDock_, &QWidget::hide);
        if (twoPortResponse_) {
            response_->setTerminated(false);
        }
        connect(response_, &FrequencyResponseDialog::frequencySelected, this, [this](double frequency) {
            change([frequency](rf::Document& d) {
                d.frequencyMHz = rf::nearestMeasuredFrequency(d, frequency);
            });
        });
        connect(response_, &FrequencyResponseDialog::frequencyTuneRequested, this, [this](int direction) {
            tune(direction, Qt::ControlModifier);
        });
        connect(response_, &FrequencyResponseDialog::displayOptionsChanged, this,
                [this](bool gmax, bool k, bool b1, bool automaticScale,
                       double ceilingDb, double floorDb) {
                    change([=](rf::Document& d) {
                        d.stabilityGmax = gmax;
                        d.stabilityK = k;
                        d.stabilityB1 = b1;
                        d.magnitudeAutoScale = automaticScale;
                        d.magnitudeCeilingDb = ceilingDb;
                        d.magnitudeFloorDb = floorDb;
                    });
                });
        connect(response_, &FrequencyResponseDialog::sweepChanged, this,
                [this](double startMHz, double stopMHz, int points,
                       bool logarithmic, double portOhms, bool terminated) {
                    change([=](rf::Document& d) {
                        d.frequencyResponseConfigured = true;
                        d.frequencyResponseStartMHz = startMHz;
                        d.frequencyResponseStopMHz = stopMHz;
                        d.frequencyResponsePoints = points;
                        d.frequencyResponseLogarithmic = logarithmic;
                        d.frequencyResponsePortOhms = portOhms;
                        d.frequencyResponseTerminated = terminated;
                    });
                });
    } else {
        response_->setNetwork(document_);
    }
}

void MainWindow::showFrequencyResponse()
{
    ensureFrequencyResponse();
    responseDock_->show();
    response_->show();
    responseDock_->raise();
    response_->focusPlot();
}

void MainWindow::showResponsePanel()
{
    const bool first = !response_;
    showFrequencyResponse();
    if (first && !isMaximized() && !isFullScreen()) resize(width() + 820, height());
}

bool MainWindow::change(const std::function<void(rf::Document&)>& mutation)
{
    try {
        auto next = document_;
        mutation(next);
        if (next.measuredLoad.has_value()
            || std::any_of(next.components.begin(), next.components.end(),
                           [](const rf::Component& c) { return c.kind == rf::Kind::Measured; }))
            next.frequencyMHz = rf::nearestMeasuredFrequency(next, next.frequencyMHz);
        if (next == document_) return true;
        auto result = rf::evaluate(next);
        undo_.push_back(document_);
        if (undo_.size() > 200) undo_.erase(undo_.begin());
        redo_.clear();
        document_ = std::move(next);
        evaluation_ = std::move(result);
        refresh();
        return true;
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Cannot change network"), e.what());
        return false;
    }
}

void MainWindow::chooseTouchstone()
{
    const auto path = QFileDialog::getOpenFileName(this, tr("Import Touchstone block or load"), {},
        tr("Touchstone 1.x (*.s1p *.s2p *.S1P *.S2P);;All files (*)"));
    if (!path.isEmpty()) importTouchstone(path);
}

void MainWindow::chooseReferenceOverlay()
{
    const auto path = QFileDialog::getOpenFileName(this, tr("Import reference Touchstone overlay"), {},
        tr("Touchstone 1.x (*.s1p *.s2p *.S1P *.S2P);;All files (*)"));
    if (!path.isEmpty()) importReferenceOverlay(path);
}

bool MainWindow::importReferenceOverlay(const QString& path)
{
    try {
        const auto suffix = QFileInfo(path).suffix().toLower();
        if (suffix != "s1p" && suffix != "s2p")
            throw std::invalid_argument("Choose a Touchstone .s1p or .s2p reference file.");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error(file.errorString().toStdString());
        if (file.size() > 16 * 1024 * 1024)
            throw std::invalid_argument("Touchstone files must be at most 16 MiB.");
        const auto bytes = file.readAll();
        if (file.error() != QFileDevice::NoError)
            throw std::runtime_error(file.errorString().toStdString());
        const auto data = rf::parseTouchstone(std::string_view(bytes.constData(), bytes.size()),
            suffix == "s1p" ? 1 : 2, QFileInfo(path).fileName().toStdString());
        return change([&](rf::Document& d) {
            d.referenceOverlay = data;
            d.referenceOverlayVisible = true;
        });
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Cannot import reference overlay"), e.what());
        return false;
    }
}

void MainWindow::clearReferenceOverlay()
{
    change([](rf::Document& d) {
        d.referenceOverlay.reset();
        d.referenceOverlayVisible = false;
    });
}

bool MainWindow::importTouchstone(const QString& path)
{
    try {
        const auto suffix = QFileInfo(path).suffix().toLower();
        if (suffix != "s1p" && suffix != "s2p")
            throw std::invalid_argument("Choose a Touchstone .s1p or .s2p file.");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error(file.errorString().toStdString());
        if (file.size() > 16 * 1024 * 1024)
            throw std::invalid_argument("Touchstone files must be at most 16 MiB.");
        const auto bytes = file.readAll();
        if (file.error() != QFileDevice::NoError)
            throw std::runtime_error(file.errorString().toStdString());
        const auto data = rf::parseTouchstone(std::string_view(bytes.constData(), bytes.size()),
            suffix == "s1p" ? 1 : 2, QFileInfo(path).fileName().toStdString());
        if (!change([&](rf::Document& d) {
            if (data.ports == 1) {
                d.measuredLoad = data;
                d.frequencyResponseConfigured = true;
                d.frequencyResponseTerminated = true;
                d.frequencyResponseStartMHz = data.samples.front().frequencyMHz;
                d.frequencyResponseStopMHz = data.samples.back().frequencyMHz;
                d.frequencyResponsePoints = 401;
                d.frequencyResponseLogarithmic = false;
                d.frequencyResponsePortOhms = data.referenceOhms;
            }
            else {
                auto c = rf::defaultComponent(rf::Kind::Measured, d.frequencyMHz, d.referenceOhms);
                c.measured = data;
                d.components.insert(d.components.begin() + d.selected + 1, std::move(c));
                ++d.selected;
                d.frequencyResponseConfigured = true;
                d.frequencyResponseTerminated = false;
                d.frequencyResponseStartMHz = data.samples.front().frequencyMHz;
                d.frequencyResponseStopMHz = data.samples.back().frequencyMHz;
                d.frequencyResponsePoints = 401;
                d.frequencyResponseLogarithmic = false;
                d.frequencyResponsePortOhms = data.referenceOhms;
            }
            const auto range = rf::measuredFrequencyRange(d);
            d.frequencyMHz = std::clamp(d.frequencyMHz, range.first, range.second);
        })) return false;
        showFrequencyResponse();
        statusBar()->showMessage(tr("Imported %1; samples embedded in the project. No extrapolation.")
                                    .arg(QFileInfo(path).fileName()), 10000);
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Cannot import Touchstone"), e.what());
        return false;
    }
}

void MainWindow::addComponent(rf::Kind kind)
{
    const auto value = editComponent(this, rf::defaultComponent(kind, document_.frequencyMHz, document_.referenceOhms));
    if (value) {
        change([&](rf::Document& d) {
            d.components.insert(d.components.begin() + d.selected + 1, *value);
            ++d.selected;
        });
        chart_->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::addComponent(rf::Kind kind, rf::Connection connection){
    auto component = rf::defaultComponent(kind, document_.frequencyMHz, document_.referenceOhms);
    component.connection = connection;  // the editor opens with this connection selected
    const auto value = editComponent(this, component);
    if (value) {
        change([&](rf::Document& d) {
            d.components.insert(d.components.begin() + d.selected + 1, *value);
            ++d.selected;
        });
        chart_->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::editSelected()
{
    if (document_.selected < 0) return;
    const auto value = editComponent(this, document_.components[static_cast<std::size_t>(document_.selected)]);
    if (value) {
        change([&](rf::Document& d) { d.components[static_cast<std::size_t>(d.selected)] = *value; });
        chart_->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::deleteSelected()
{
    if (document_.selected < 0) return;
    change([](rf::Document& d) {
        d.components.erase(d.components.begin() + d.selected);
        d.selected = d.components.empty() ? -1 : std::max(0, d.selected - 1);
    });
}

void MainWindow::writeClipboardComponent(const rf::Component& component)
{
    clipboardComponent_ = component;
    clipboardPlainText_ = tr("RFdude component: %1")
        .arg(componentDescription(component));
    auto* mime = new QMimeData;
    mime->setText(clipboardPlainText_);
    mime->setData(QString::fromLatin1(componentMimeType),
                  documents::componentToJson(component));
    QGuiApplication::clipboard()->setMimeData(mime);
}

std::optional<rf::Component> MainWindow::readClipboardComponent()
{
    const auto* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) return std::nullopt;
    const auto format = QString::fromLatin1(componentMimeType);
    if (mime->hasFormat(format)) {
        try {
            clipboardComponent_ = documents::componentFromJson(mime->data(format));
            clipboardPlainText_ = mime->text();
            return clipboardComponent_;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    if (clipboardComponent_ && mime->text() == clipboardPlainText_)
        return clipboardComponent_;
    return std::nullopt;
}

void MainWindow::copySelected()
{
    if (document_.selected < 0
        || document_.selected >= static_cast<int>(document_.components.size()))
        return;
    writeClipboardComponent(document_.components[static_cast<std::size_t>(document_.selected)]);
    statusBar()->showMessage(tr("Copied selected component."));
}

void MainWindow::cutSelected()
{
    if (document_.selected < 0
        || document_.selected >= static_cast<int>(document_.components.size()))
        return;
    writeClipboardComponent(document_.components[static_cast<std::size_t>(document_.selected)]);
    change([](rf::Document& d) {
        d.components.erase(d.components.begin() + d.selected);
        d.selected = d.components.empty() ? -1
                                         : std::min(d.selected,
                                                    static_cast<int>(d.components.size()) - 1);
    });
    statusBar()->showMessage(tr("Cut selected component."));
}

void MainWindow::pasteComponent()
{
    if (document_.components.size() >= 1000) {
        statusBar()->showMessage(tr("A network may contain at most 1000 components."));
        return;
    }
    const auto component = readClipboardComponent();
    if (!component) return;
    const bool changed = change([&](rf::Document& d) {
        const int index = d.selected >= 0 ? d.selected : 0;
        d.components.insert(d.components.begin() + index, *component);
        d.selected = index;
    });
    if (changed) {
        statusBar()->showMessage(tr("Pasted component to the right of the selection."));
        chart_->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::insertComponentRight()
{
    if (document_.components.size() >= 1000) {
        statusBar()->showMessage(tr("A network may contain at most 1000 components."));
        return;
    }
    rf::Kind kind = rf::Kind::Capacitor;
    if (document_.selected >= 0
        && document_.selected < static_cast<int>(document_.components.size())) {
        const auto selectedKind =
            document_.components[static_cast<std::size_t>(document_.selected)].kind;
        if (selectedKind != rf::Kind::Measured)
            kind = selectedKind;
    }
    const auto value = editComponent(
        this, rf::defaultComponent(kind, document_.frequencyMHz, document_.referenceOhms));
    if (value) {
        const bool changed = change([&](rf::Document& d) {
            const int index = d.selected >= 0 ? d.selected : 0;
            d.components.insert(d.components.begin() + index, *value);
            d.selected = index;
        });
        if (changed)
            statusBar()->showMessage(tr("Inserted component to the right of the selection."));
        chart_->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::select(int index)
{
    if (index < 0 || index >= static_cast<int>(document_.components.size())) return;
    document_.selected = index;
    refresh();
}

void MainWindow::tune(int direction, Qt::KeyboardModifiers modifiers)
{
    change([&](rf::Document& d) {
        const double multiplier = 1 + direction * d.tunePercent / 100;
        const bool measured = d.measuredLoad.has_value()
            || std::any_of(d.components.begin(), d.components.end(),
                           [](const rf::Component& c) { return c.kind == rf::Kind::Measured; });
        if (modifiers.testFlag(Qt::ControlModifier)) {
            d.frequencyMHz = measured
                ? rf::stepMeasuredFrequency(d, d.frequencyMHz, direction)
                : d.frequencyMHz * multiplier;
        } else if (modifiers.testFlag(Qt::ShiftModifier)) {
            d.referenceOhms *= multiplier;
        } else if (response_ && response_->isVisible() && response_->plotHasFocus()) {
            d.frequencyMHz = measured
                ? rf::stepMeasuredFrequency(d, d.frequencyMHz, direction)
                : d.frequencyMHz * multiplier;
        }
        else if (d.selected >= 0)
            rf::tune(d.components[static_cast<std::size_t>(d.selected)], d.tunePercent, direction > 0);
    });
}

void MainWindow::settings(const QString& focus)
{
    const auto value = editSettings(this, document_, focus);
    if (value) change([&](rf::Document& d) { d = *value; });
}

void MainWindow::undo()
{
    if (undo_.empty()) return;
    redo_.push_back(document_);
    document_ = undo_.back();
    undo_.pop_back();
    evaluation_ = rf::evaluate(document_);
    refresh();
}

void MainWindow::redo()
{
    if (redo_.empty()) return;
    undo_.push_back(document_);
    document_ = redo_.back();
    redo_.pop_back();
    evaluation_ = rf::evaluate(document_);
    refresh();
}

bool MainWindow::confirmDiscard()
{
    if (!isModified()) return true;
    const auto result = QMessageBox::question(this, tr("Unsaved network"),
        tr("Save changes before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (result == QMessageBox::Save) return chooseSave();
    return result == QMessageBox::Discard;
}

void MainWindow::newDocument()
{
    if (!confirmDiscard()) return;
    document_ = rf::Document{};
    saved_ = document_;
    imported_ = false;
    path_.clear();
    undo_.clear();
    redo_.clear();
    evaluation_ = rf::evaluate(document_);
    refresh();
}

void MainWindow::chooseOpen()
{
    if (!confirmDiscard()) return;
    const auto path = QFileDialog::getOpenFileName(this, tr("Open or import network"), {},
        tr("RFdude projects (*.rfs.json *.json *.lls);;All files (*)"));
    if (!path.isEmpty()) openDocument(path);
}

void MainWindow::chooseExample()
{
    const QDir application(QCoreApplication::applicationDirPath());
    QString directory;
    for (const auto& relative : {"../share/RFdudeSmithChartV2/examples",
                                 "examples", "../examples",
                                 "../../../share/RFdudeSmithChartV2/examples"}) {
        const QDir candidate(application.filePath(relative));
        if (candidate.exists()) {
            directory = candidate.canonicalPath();
            break;
        }
    }
    if (directory.isEmpty()) {
        QMessageBox::warning(this, tr("Examples not found"),
            tr("The examples directory is missing. Reinstall the application to restore "
               "the bundled examples, or use Open / import to open a project."));
        return;
    }
    if (!confirmDiscard()) return;
    const auto path = QFileDialog::getOpenFileName(this, tr("Open example"), directory,
        tr("RFdude projects (*.rfs.json *.json *.lls);;All files (*)"));
    if (!path.isEmpty() && openDocument(path)) {
        // Examples are templates, not user documents to overwrite on Save.
        path_.clear();
        imported_ = true;
        refresh();
        statusBar()->showMessage(tr("Example opened. Save creates your own project copy."), 10000);
    }
}

bool MainWindow::openDocument(const QString& path)
{
    try {
        bool legacy = false;
        auto next = documents::load(path, &legacy);
        next.frequencyMHz = rf::clampMeasuredFrequency(next, next.frequencyMHz);
        auto result = rf::evaluate(next);
        document_ = std::move(next);
        evaluation_ = std::move(result);
        saved_ = document_;
        imported_ = legacy;
        path_ = legacy ? QString{} : path;
        undo_.clear();
        redo_.clear();
        refresh();
        statusBar()->showMessage(legacy ? tr("Legacy project imported. Save as a new .rfs.json file.")
                                       : tr("Opened %1").arg(path), 10000);
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Cannot open project"), e.what());
        return false;
    }
}

bool MainWindow::chooseSave(bool saveAs)
{
    QString path = path_;
    if (saveAs || path.isEmpty())
        path = QFileDialog::getSaveFileName(this, tr("Save network"), path.isEmpty() ? "network.rfs.json" : path,
            tr("RFdude V2 project (*.rfs.json)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(".json", Qt::CaseInsensitive)) path += ".rfs.json";
    return saveDocument(path);
}

bool MainWindow::saveDocument(const QString& path)
{
    try {
        if (path.endsWith(".lls", Qt::CaseInsensitive))
            throw std::invalid_argument("Legacy projects are import-only. Save with the .rfs.json extension.");
        documents::save(path, document_);
        path_ = path;
        saved_ = document_;
        imported_ = false;
        refresh();
        statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Cannot save project"), e.what());
        return false;
    }
}

void MainWindow::inspectImpedance(double real, double imaginary, QPoint position)
{
    const rf::Complex value(real, imaginary);
    QMenu menu(this);
    auto* label = menu.addAction(impedanceText(rf::Impedance::fromOhms(value), document_.polar));
    label->setEnabled(false);
    auto* start = menu.addAction(tr("Use as starting impedance"));
    auto* goal = menu.addAction(tr("Use as goal impedance"));
    const auto* result = menu.exec(position);
    if (result == start) change([&](rf::Document& d) { d.startOhms = value; d.measuredLoad.reset(); });
    else if (result == goal) change([&](rf::Document& d) { d.goalOhms = value; });
}

void MainWindow::exportChart()
{
    auto path = QFileDialog::getSaveFileName(this, tr("Export network report"), "smith-chart.png", "PNG image (*.png)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    try {
        if (!chartImage().save(path, "PNG"))
            throw std::runtime_error(tr("Could not write chart image to %1.").arg(path).toStdString());
        statusBar()->showMessage(tr("Exported %1").arg(path), 5000);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Export failed"), e.what());
    }
}

QImage MainWindow::chartImage()
{
    ensureFrequencyResponse();
    auto response = response_->responseImage();
    chart_->setFocusBorderEnabled(false);
    const auto chart = chart_->grab();
    chart_->setFocusBorderEnabled(true);
    const int margin = 20;
    const int width = std::max(1000, chart_->width() + 2 * margin);
    if (response.width() != width - 2 * margin)
        response = response.scaledToWidth(width - 2 * margin, Qt::SmoothTransformation);
    QString html = "<h2>" + tr("Network / circuit description").toHtmlEscaped() + "</h2><p>"
        + summary_->text().toHtmlEscaped().replace("\n", "<br>") + "</p>";
    if (document_.measuredLoad)
        html += "<p>" + measuredDescription(*document_.measuredLoad).toHtmlEscaped() + "</p>";
    if (table_->rowCount() == 0) {
        html += "<p>" + tr("No components (starting impedance only).").toHtmlEscaped() + "</p>";
    } else {
        html += "<table width=\"100%\" border=\"1\" cellspacing=\"0\" cellpadding=\"6\"><tr>";
        for (int column = 0; column < table_->columnCount(); ++column)
            html += "<th>" + table_->horizontalHeaderItem(column)->text().toHtmlEscaped() + "</th>";
        html += "</tr>";
        for (int row = 0; row < table_->rowCount(); ++row) {
            html += "<tr>";
            for (int column = 0; column < table_->columnCount(); ++column)
                html += "<td>" + table_->item(row, column)->text().toHtmlEscaped() + "</td>";
            html += "</tr>";
        }
        html += "</table>";
    }
    html += "<p>" + metrics_->text().toHtmlEscaped().replace("\n", "<br>") + "</p>";
    QTextDocument description;
    description.setDefaultFont(QFont("sans-serif", 10));
    description.setDefaultStyleSheet("body { color: black; background-color: white; }");
    description.setHtml("<body>" + html + "</body>");
    description.setTextWidth(width - 2 * margin);
    const int descriptionHeight = static_cast<int>(std::ceil(description.size().height()));
    const int schematicHeight = schematic_->reportHeight(width - 2 * margin);
    QImage image(width, chart_->height() + schematicHeight + descriptionHeight + response.height() + 5 * margin,
                 QImage::Format_RGB32);
    if (image.isNull()) return image;
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.drawPixmap(QRect((width - chart_->width()) / 2, margin,
                             chart_->width(), chart_->height()), chart);
    painter.setRenderHint(QPainter::Antialiasing);
    schematic_->paintReport(painter, QPointF(margin, chart_->height() + 2 * margin),
                             width - 2 * margin);
    painter.drawImage(QPoint(margin, chart_->height() + schematicHeight + 3 * margin), response);
    painter.translate(margin, chart_->height() + schematicHeight + response.height() + 4 * margin);
    description.drawContents(&painter);
    return image;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        const bool componentView = watched == table_ || watched == table_->viewport()
            || watched == chart_ || watched == schematic_ || watched == schematic_->viewport();
        if (componentView && !QApplication::activeModalWidget()) {
            const auto* keyEvent = static_cast<const QKeyEvent*>(event);
            const bool primaryModifier = keyEvent->modifiers().testFlag(Qt::ControlModifier)
                || keyEvent->modifiers().testFlag(Qt::MetaModifier);
            if (primaryModifier
                && !keyEvent->modifiers().testFlag(Qt::ShiftModifier)
                && !keyEvent->modifiers().testFlag(Qt::AltModifier)) {
                switch (keyEvent->key()) {
                case Qt::Key_I:
                    insertComponentRight();
                    return true;
                case Qt::Key_X:
                    cutSelected();
                    return true;
                case Qt::Key_C:
                    copySelected();
                    return true;
                case Qt::Key_V:
                    pasteComponent();
                    return true;
                default:
                    break;
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (confirmDiscard()) event->accept();
    else event->ignore();
}

void MainWindow::exportLtspice()
{
    QFileDialog dialog(this, tr("Export LTspice schematic"), {},
                       tr("LTspice schematic (*.asc)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix("asc");
    dialog.selectFile("smith-network.asc");
    if (dialog.exec() != QDialog::Accepted) return;
    const auto path = dialog.selectedFiles().front();
    try {
        ltspice::save(path, document_);
        statusBar()->showMessage(tr("Exported %1").arg(path), 5000);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("LTspice export failed"), e.what());
    }
}
