#pragma once
#include <BWAPI.h>

namespace SYSIA::Strategy {

    std::string getEnemyBuild();
    std::string getEnemyOpener();
    std::string getEnemyTransition();
    BWAPI::Position enemyScoutPosition();
    Time getEnemyBuildTime();
    Time getEnemyOpenerTime();
    Time getEnemyTransitionTime();

    int getWorkersNearUs();
    bool enemyFastExpand();
    bool enemyRush();
    bool needDetection();
    bool defendChoke();
    bool enemyAir();
    bool enemyPossibleProxy();
    bool enemyProxy();
    bool enemyGasSteal();
    bool enemyScouted();
    bool enemyBust();
    bool enemyPressure();
    bool enemyBlockedScout();
    bool enemyWalled();
    Time enemyArrivalTime();

    void onFrame();
}
