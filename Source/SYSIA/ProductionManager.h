#pragma once
#include <BWAPI.h>

namespace SYSIA::Production
{
    int getReservedMineral();
    int getReservedGas();
    double scoreUnit(BWAPI::UnitType);
    bool hasIdleProduction();
    void onFrame();
}