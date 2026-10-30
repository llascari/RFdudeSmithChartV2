#pragma once

#include "RfModel.h"

#include <QMainWindow>
#include <QImage>
#include <functional>

class QLabel;
class QTableWidget;
class SmithChartWidget;
class SchematicWidget;
class FrequencyResponseDialog;
class QAction;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    const rf::Document& document() const { return document_; }
    bool openDocument(const QString& path);
    bool saveDocument(const QString& path);
    bool isModified() const;
    QImage chartImage();
    bool importTouchstone(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildActions();
    void refresh();
    bool change(const std::function<void(rf::Document&)>& mutation);
    void addComponent(rf::Kind kind);
    void editSelected();
    void deleteSelected();
    void select(int index);
    void tune(int direction, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void settings(const QString& focus = {});
    void undo();
    void redo();
    void newDocument();
    void chooseOpen();
    void chooseExample();
    bool chooseSave(bool saveAs = false);
    bool confirmDiscard();
    void inspectImpedance(double real, double imaginary, QPoint position);
    void exportChart();
    void exportLtspice();
    void showFrequencyResponse();
    void chooseTouchstone();
    void ensureFrequencyResponse();

    rf::Document document_;
    rf::Document saved_;
    rf::Evaluation evaluation_;
    std::vector<rf::Document> undo_;
    std::vector<rf::Document> redo_;
    QString path_;
    bool imported_ = false;
    SmithChartWidget* chart_;
    SchematicWidget* schematic_;
    FrequencyResponseDialog* response_ = nullptr;
    QTableWidget* table_;
    QLabel* summary_;
    QLabel* metrics_;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* editAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    std::vector<QAction*> displayActions_;
};
