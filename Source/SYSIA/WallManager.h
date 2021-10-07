#pragma once
#include <BWAPI.h>

namespace SYSIA::Walls {

    void onStart();
    
    int needGroundDefenses(BWEB::Wall&);
    int needAirDefenses(BWEB::Wall&);
    BWEB::Wall* getMainWall();
    BWEB::Wall* getNaturalWall();
}
