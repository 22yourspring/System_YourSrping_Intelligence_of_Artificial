#pragma once
#include <BWAPI.h>

namespace SYSIA::Workers {

    void onFrame();
    void removeUnit(UnitInfo&);

    int getMineralWorkers();
    int getGasWorkers();
    int getBoulderWorkers();
    bool shouldMoveToBuild(UnitInfo&, BWAPI::TilePosition, BWAPI::UnitType);
}
