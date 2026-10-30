#pragma once

#include "RfModel.h"

#include <optional>

class QWidget;

std::optional<rf::Document> matchWizard(QWidget* parent, const rf::Document& document);
std::optional<rf::Document> filterWizard(QWidget* parent, const rf::Document& document);
