#pragma once
#include <BWAPI.h>

namespace SYSIA::Combat {

    void onStart();
    void onFrame();
    std::multimap<double, BWAPI::Position>& getCombatClusters();
    BWAPI::Position getClosestRetreatPosition(UnitInfo&);
    BWAPI::Position getAirClusterCenter();
    std::set<BWAPI::Position> getDefendPositions();
    void resetDefendPositionCache();
}