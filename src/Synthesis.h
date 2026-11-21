#pragma once

#include "RfModel.h"

namespace rf {

enum class MatchType { LowPass, HighPass, QuarterWave, SingleStub, DoubleStub };
enum class StubTermination { Open, Shorted };

struct MatchSpecification
{
    MatchType type = MatchType::LowPass;
    int sections = 1;                                // Ignored by the stub topologies.
    StubTermination stub = StubTermination::Open;    // Used only by the stub topologies.
    Complex startOhms = {5, 0};
    Complex goalOhms = {50, 0};
    double frequencyMHz = 1000;
    double referenceOhms = 50;
};

Document synthesizeMatch(const MatchSpecification& specification);

enum class FilterType { LowPass, HighPass };

struct FilterSpecification
{
    FilterType type = FilterType::LowPass;
    int order = 3;
    double cutoffMHz = 1000;
    double impedanceOhms = 50;
    double rippleDb = 0.1;
    bool shuntFirst = true;
};

Document synthesizeFilter(const FilterSpecification& specification);

}