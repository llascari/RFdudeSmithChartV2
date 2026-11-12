#pragma once

#include "RfModel.h"

#include <QWidget>

#include <utility>
#include <vector>

class QToolButton;

/// @brief Toolbar widget for adding components: one group box per component type.
///
/// Each group box holds one icon button per connection: series / shunt for the lumped
/// elements, through / open stub / short stub for the transmission lines.
///
/// The icons are drawn with the schematic's own symbol code (paintComponentSymbol()), so
/// they always match what ends up in the schematic. They use the current text colour and
/// are redrawn when the palette or style changes.
///
/// The widget only reports the user's choice through addRequested(). It does not open the
/// component editor or modify the network itself.
class ComponentPalette final : public QWidget {
    Q_OBJECT

public:
    /// @brief Creates the palette with all its group boxes and buttons.
    /// @param parent Parent widget, normally the toolbar that hosts the palette.
    explicit ComponentPalette(QWidget* parent = nullptr);

signals:
    /// @brief Emitted when the user clicks one of the configuration buttons.
    /// @param kind Type of component to add.
    /// @param connection How the component is connected (series, shunt, through line,
    ///        open stub or short stub). The component editor should open with it selected.
    void addRequested(rf::Kind kind, rf::Connection connection);

protected:
    /// @brief Redraws the icons when the palette or the style changes.
    /// @param event The change event; only QEvent::PaletteChange and QEvent::StyleChange
    ///        trigger a redraw.
    void changeEvent(QEvent* event) override;

private:
    /// @brief Redraws every button's icon with the current palette and device pixel ratio
    void refreshIcons();

    /// All buttons, each with the component kind and connection it add
    std::vector<std::pair<QToolButton*, std::pair<rf::Kind, rf::Connection>>> buttons_;
};
