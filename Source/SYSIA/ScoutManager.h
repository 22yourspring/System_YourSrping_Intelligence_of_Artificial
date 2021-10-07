#pragma once
#include <BWAPI.h>

namespace SYSIA::Scouts
{
    void onFrame();
    void removeUnit(UnitInfo&);
    bool gotFullScout();
    bool isSacrificeScout();
}