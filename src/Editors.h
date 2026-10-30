#pragma once

#include "RfModel.h"

#include <QString>
#include <optional>

class QWidget;

std::optional<rf::Component> editComponent(QWidget* parent, const rf::Component& component);
std::optional<rf::Document> editSettings(QWidget* parent, const rf::Document& document,
                                       const QString& focusField = {});
QString componentDescription(const rf::Component& component);
QString measuredDescription(const rf::TouchstoneData& data);
