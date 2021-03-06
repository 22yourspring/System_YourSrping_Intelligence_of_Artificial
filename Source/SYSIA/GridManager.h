#pragma once
#include <BWAPI.h>

namespace SYSIA::Grids
{
    void onFrame();
    void onStart();

    float getAGroundCluster(BWAPI::WalkPosition here);
    float getAGroundCluster(BWAPI::Position here);

    float getAAirCluster(BWAPI::WalkPosition here);
    float getAAirCluster(BWAPI::Position here);

    float getEGroundThreat(BWAPI::WalkPosition here);
    float getEGroundThreat(BWAPI::Position here);

    float getEAirThreat(BWAPI::WalkPosition here);
    float getEAirThreat(BWAPI::Position here);

    float getEGroundCluster(BWAPI::WalkPosition here);
    float getEGroundCluster(BWAPI::Position here);
    float getEAirCluster(BWAPI::WalkPosition here);
    float getEAirCluster(BWAPI::Position here);

    int getCollision(BWAPI::WalkPosition here);


    int getESplash(BWAPI::WalkPosition here);

    int getMobility(BWAPI::WalkPosition here);
    int getMobility(BWAPI::Position here);

    int lastVisibleFrame(BWAPI::TilePosition t);
    int lastVisitedFrame(BWAPI::WalkPosition w);

    void addMovement(BWAPI::Position, UnitInfo&);
}