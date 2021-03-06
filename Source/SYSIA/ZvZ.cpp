#include "SYSIA.h"

using namespace std;
using namespace BWAPI;
using namespace UnitTypes;
using namespace SYSIA::BuildOrder::All;

namespace SYSIA::BuildOrder::Zerg {

    namespace {

        bool lingSpeed() {
            return Broodwar->self()->isUpgrading(UpgradeTypes::Metabolic_Boost) || Broodwar->self()->getUpgradeLevel(UpgradeTypes::Metabolic_Boost);
        }

        bool gas(int amount) {
            return Broodwar->self()->gas() >= amount;
        }

        int gasMax() {
            return Resources::getGasCount() * 3;
        }

        int capGas(int value) {
            auto onTheWay = 0;
            for (auto &w : Units::getUnits(PlayerState::Self)) {
                auto &worker = *w;
                if (worker.unit()->isCarryingGas())
                    onTheWay+=8;
            }

            return int(round(double(value - Broodwar->self()->gas() + onTheWay) / 8.0));
        }

        int hatchCount() {
            return vis(Zerg_Hatchery) + vis(Zerg_Lair) + vis(Zerg_Hive);
        }

        int colonyCount() {
            return vis(Zerg_Creep_Colony) + vis(Zerg_Sunken_Colony);
        }

        int lingsNeeded() {
            if (Players::getTotalCount(PlayerState::Enemy, Zerg_Sunken_Colony) >= 1 && Players::getTotalCount(PlayerState::Enemy, Zerg_Zergling) < 6)
                return 24;
            else if (Strategy::getEnemyTransition() == "2HatchSpeedling")
                return 0;
            else if (vis(Zerg_Spire) > 0)
                return 16;
            else if (vis(Zerg_Lair) > 0)
                return 18;
            else
                return 8;
        }

        void defaultZvZ() {
            inOpeningBook =                                 true;
            inBookSupply =                                  true;
            wallNat =                                       hatchCount() >= 3;
            wallMain =                                      false;
            scout =                                         false;
            wantNatural =                                   false;
            wantThird =                                     false;
            proxy =                                         false;
            hideTech =                                      false;
            playPassive =                                   false;
            rush =                                          false;
            pressure =                                      false;
            cutWorkers =                                    false;
            transitionReady =                               false;

            gasLimit =                                      gasMax();
            lingLimit =                                     lingsNeeded();
            droneLimit =                                    INT_MAX;

            desiredDetection =                              Zerg_Overlord;
            firstUpgrade =                                  vis(Zerg_Zergling) >= 6 ? UpgradeTypes::Metabolic_Boost : UpgradeTypes::None;
            firstTech =                                     TechTypes::None;
            firstUnit =                                     None;

            armyComposition[Zerg_Drone] =                   0.60;
            armyComposition[Zerg_Zergling] =                0.40;
        }
    }

    void ZvZPoolLair()
    {
        defaultZvZ();

        // Openers
        if (currentOpener == "9Pool") {
            transitionReady =                               lingSpeed();
            droneLimit =                                    9 - (vis(Zerg_Extractor) > 0) + (vis(Zerg_Overlord) > 1);
            lingLimit =                                     Strategy::enemyRush() ? INT_MAX : 10;
            gasLimit =                                      Strategy::enemyRush() && com(Zerg_Sunken_Colony) == 0 ? 0 : gasMax();
            playPassive =                                   (com(Zerg_Mutalisk) == 0 && Strategy::enemyRush()) || (Strategy::getEnemyOpener() == "9Pool" && Players::getTotalCount(PlayerState::Enemy, Zerg_Zergling) >= 8 && !Strategy::enemyRush() && !Strategy::enemyPressure() && total(Zerg_Mutalisk) == 0);

            buildQueue[Zerg_Spawning_Pool] =                s >= 18;
            buildQueue[Zerg_Extractor] =                    s >= 18 && vis(Zerg_Spawning_Pool) > 0;
            buildQueue[Zerg_Overlord] =                     1 + (vis(Zerg_Extractor) >= 1) + (s >= 32);
        }


        // Reactions
        if (!lockedTransition) {

        }
        if (!transitionReady)
            return;

        // 'https://liquipedia.net/starcraft/9_Pool_Speed_into_1_Hatch_Spire_(vs._Zerg)'
        if (currentTransition == "1HatchMuta") {
            inOpeningBook =                                 total(Zerg_Mutalisk) < 9;
            lockedTransition =                              vis(Zerg_Lair) > 0;
            droneLimit =                                    atPercent(Zerg_Lair, 0.50) ? 12 : 9;
            lingLimit =                                     lingsNeeded();
            gasLimit =                                      (lingSpeed() && com(Zerg_Lair) == 0) ? 2 - (vis(Zerg_Lair) > 0 && !atPercent(Zerg_Lair, 0.75)) : gasMax();
            firstUnit =                                     Zerg_Mutalisk;
            inBookSupply =                                  vis(Zerg_Overlord) < 4 && total(Zerg_Mutalisk) < 3;

            // Build
            buildQueue[Zerg_Lair] =                         gas(100) && vis(Zerg_Spawning_Pool) > 0 && total(Zerg_Zergling) >= 6 && vis(Zerg_Drone) >= 8;
            buildQueue[Zerg_Spire] =                        lingSpeed() && atPercent(Zerg_Lair, 0.80) && vis(Zerg_Drone) >= 9;
            buildQueue[Zerg_Hatchery] =                     1 + (Players::getVisibleCount(PlayerState::Enemy, Zerg_Sunken_Colony) >= 2 && Util::getTime() < Time(3, 30));
            buildQueue[Zerg_Overlord] =                     1 + (vis(Zerg_Extractor) >= 1) + (s >= 32) + (atPercent(Zerg_Spire, 0.5) && s >= 38);

            // Army Composition
            if (com(Zerg_Spire) > 0) {
                armyComposition[Zerg_Drone] =               0.40;
                armyComposition[Zerg_Zergling] =            0.00;
                armyComposition[Zerg_Mutalisk] =            0.60;
            }
            else {
                armyComposition[Zerg_Drone] =               0.50;
                armyComposition[Zerg_Zergling] =            0.50;
            }
        }
    }

    void ZvZPoolHatch()
    {
        defaultZvZ();

        // 'https://liquipedia.net/starcraft/Overpool_(vs._Zerg)'
        if (currentOpener == "Overpool") {                  // 9 Overlord, 9 Pool, 11 Hatch
            transitionReady =                               total(Zerg_Zergling) >= 6 || (Strategy::enemyFastExpand() && com(Zerg_Spawning_Pool) > 0);
            droneLimit =                                    Strategy::enemyFastExpand() ? 16 : 10;
            lingLimit =                                     6;
            gasLimit =                                      capGas(100);
            playPassive =                                   (com(Zerg_Mutalisk) == 0 && Strategy::enemyRush()) || (Strategy::getEnemyOpener() == "9Pool" && Players::getTotalCount(PlayerState::Enemy, Zerg_Zergling) >= 8 && !Strategy::enemyRush() && !Strategy::enemyPressure() && total(Zerg_Mutalisk) == 0);

            buildQueue[Zerg_Hatchery] =                     1 + (s >= 22);
            buildQueue[Zerg_Spawning_Pool] =                (vis(Zerg_Overlord) >= 2);
            buildQueue[Zerg_Overlord] =                     1 + (s >= 18) + (s >= 32);
        }

        // 'https://liquipedia.net/starcraft/12_Pool_in-base_hatch_(vs._Zerg)'
        if (currentOpener == "12Pool") {                    // 12 Pool, 12 Gas, 11 Hatch
            transitionReady =                               total(Zerg_Zergling) >= 6;
            droneLimit =                                    vis(Zerg_Extractor) > 0 ? 9 : 12;
            lingLimit =                                     12;
            gasLimit =                                      com(Zerg_Drone) >= 10 ? gasMax() : 0;
            firstUpgrade =                                  vis(Zerg_Zergling) >= 6 ? UpgradeTypes::Metabolic_Boost : UpgradeTypes::None;
            playPassive =                                   total(Zerg_Zergling) < 16;

            buildQueue[Zerg_Hatchery] =                     1 + (s >= 22 && vis(Zerg_Extractor) > 0);
            buildQueue[Zerg_Spawning_Pool] =                s >= 24;
            buildQueue[Zerg_Extractor] =                    (s >= 24 && vis(Zerg_Spawning_Pool) > 0);
            buildQueue[Zerg_Overlord] =                     1 + (s >= 18);
        }

        // Reactions
        if (!lockedTransition) {
            if (Strategy::getEnemyTransition() == "1HatchMuta" || Strategy::getEnemyTransition() == "2HatchMuta" || Strategy::getEnemyOpener() == "9Pool")
                currentTransition = "2HatchSpeedling";
        }
        if (!transitionReady)
            return;

        //
        if (currentTransition == "2HatchMuta") {
            inOpeningBook =                                 total(Zerg_Mutalisk) < 3;
            lockedTransition =                              vis(Zerg_Lair) > 0;
            droneLimit =                                    14;
            gasLimit =                                      (lingSpeed() && com(Zerg_Lair) == 0) ? 1 : gasMax();
            lingLimit =                                     18;

            firstUnit =                                     Zerg_Mutalisk;
            inBookSupply =                                  vis(Zerg_Overlord) < 3;
            playPassive =                                   com(Zerg_Mutalisk) == 0 && Players::getTotalCount(PlayerState::Enemy, Zerg_Zergling) > total(Zerg_Zergling);

            // Build
            buildQueue[Zerg_Extractor] =                    (hatchCount() >= 2 && vis(Zerg_Drone) >= 9) + (atPercent(Zerg_Spire, 0.5));
            buildQueue[Zerg_Lair] =                         gas(100) && vis(Zerg_Spawning_Pool) > 0 && total(Zerg_Zergling) >= 12 && vis(Zerg_Drone) >= 8;
            buildQueue[Zerg_Spire] =                        lingSpeed() && atPercent(Zerg_Lair, 0.8) && vis(Zerg_Drone) >= 9;
            buildQueue[Zerg_Overlord] =                     1 + (vis(Zerg_Extractor) >= 1) + (s >= 32) + (atPercent(Zerg_Spire, 0.5) && s >= 38);

            // Army Composition
            if (com(Zerg_Spire)) {
                armyComposition[Zerg_Drone] =               0.40;
                armyComposition[Zerg_Zergling] =            0.10;
                armyComposition[Zerg_Mutalisk] =            0.50;
            }
            else {
                armyComposition[Zerg_Drone] =               0.50;
                armyComposition[Zerg_Zergling] =            0.50;
            }
        }

        // 
        if (currentTransition == "2HatchSpeedling") {
            inOpeningBook =                                 total(Zerg_Zergling) < 50 && Strategy::getEnemyTransition() != "2HatchSpeedling";
            droneLimit =                                    9;
            lingLimit =                                     INT_MAX;
            gasLimit =                                      !lingSpeed() ? capGas(100) : (com(Zerg_Hatchery) >= 2);
            firstUpgrade =                                  UpgradeTypes::Metabolic_Boost;
            firstUnit =                                     UnitTypes::None;
            inBookSupply =                                  vis(Zerg_Overlord) < 3;
            pressure =                                      Broodwar->self()->getUpgradeLevel(UpgradeTypes::Metabolic_Boost) == 1 && Util::getTime() < Time(4, 00);
            playPassive =                                   vis(Zerg_Zergling) <= Players::getVisibleCount(PlayerState::Enemy, Zerg_Zergling) || (Strategy::enemyRush() && total(Zerg_Zergling) < 24) || total(Zerg_Zergling) < 12;

            // Build
            buildQueue[Zerg_Hatchery] =                     1 + (s >= 20 && vis(Zerg_Spawning_Pool) > 0);
            buildQueue[Zerg_Extractor] =                    (hatchCount() >= 2 && vis(Zerg_Drone) >= 9);
            buildQueue[Zerg_Overlord] =                     1 + (s >= 18) + (s >= 32);

            // Composition
            armyComposition[Zerg_Drone] =                   0.20;
            armyComposition[Zerg_Zergling] =                0.80;
        }
    }
}