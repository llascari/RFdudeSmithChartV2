#pragma once

#include "RfModel.h"

#include <string_view>
#include <utility>

namespace rf {

TouchstoneData parseTouchstone(std::string_view text, int ports, const std::string& name);
void validate(const TouchstoneData& data);
SParameters measuredParameters(const TouchstoneData& data, double frequencyMHz, double portOhms);
Impedance startingImpedance(const Document& document);
Impedance startingImpedance(const Document& document, double frequencyMHz);
std::pair<double, double> measuredFrequencyRange(const Document& document);

}
