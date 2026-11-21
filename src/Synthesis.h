#pragma once

#include "RfModel.h"

namespace rf {

enum class MatchType { LowPass, HighPass, QuarterWave, SingleStub, DoubleStub, Pi };
enum class StubTermination { Open, Shorted };
enum class ReactanceMatch { Lumped, LambdaEighth };
// Band of each half of a Pi network, goal (source) side first.
enum class PiType { LpLp, LpHp, HpLp, HpHp };

struct MatchSpecification
{
    MatchType type = MatchType::LowPass;
    int sections = 1;                                // Ignored by the stub and Pi topologies.
    StubTermination stub = StubTermination::Open;    // Used only by the stub topologies.
    ReactanceMatch startReactance = ReactanceMatch::Lumped;  // Used only by QuarterWave.
    double q = 5;                                    // Loaded Q; used only by Pi.
    PiType pi = PiType::LpLp;                        // Used only by Pi.
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