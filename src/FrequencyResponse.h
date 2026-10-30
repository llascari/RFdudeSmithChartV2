#pragma once

#include "RfModel.h"

namespace rf {

struct Sweep
{
    double startMHz = 100;
    double stopMHz = 2000;
    int points = 401;
    bool logarithmic = false;
    double portOhms = 50;
};

// Equal, real power-wave references at both ports. Port 1 is the source/input.
// The starting and goal impedances are external terminations, not part of this two-port.
SParameters sParameters(const std::vector<Component>& components, double frequencyMHz,
                        double portOhms);
Complex inputReflection(const Document& document, double frequencyMHz, double portOhms);
void validate(const Sweep& sweep);
std::vector<ResponseSample> frequencyResponse(const std::vector<Component>& components,
                                              const Sweep& sweep);

}
