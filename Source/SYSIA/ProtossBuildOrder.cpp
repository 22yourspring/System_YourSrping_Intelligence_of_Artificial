#include "SYSIA.h"

using namespace BWAPI;
using namespace std;
using namespace UnitTypes;
using namespace SYSIA::BuildOrder::All;

namespace SYSIA::BuildOrder::Protoss
{
    void opener()
    {
        // Assume a matchup
        if (Terrain::isIslandMap()) {
            if (currentBuild == "NexusGate")
                PvTNexusGate();
            else if (currentBuild == "1GateCore")
                PvP1GateCore();
        }

        if (Players::vT()) {
            if (currentBuild == "1GateCore")
                PvT1GateCore();
            else if (currentBuild == "NexusGate")
                PvTNexusGate();
            else if (currentBuild == "GateNexus")
                PvTGateNexus();
            else if (currentBuild == "2Gate")
                PvT2Gate();
        }
        else if (Players::vP()) {
            if (currentBuild == "1GateCore")
                PvP1GateCore();
            else if (currentBuild == "2Gate")
                PvP2Gate();
        }
        else if (Players::vZ() || Players::getRaceCount(Races::Unknown, PlayerState::Enemy) > 0) {
            if (currentBuild == "1GateCore")
                PvZ1GateCore();
            else if (currentBuild == "FFE")
                PvZFFE();
            else if (currentBuild == "2Gate")
                PvZ2Gate();
        }
    }

    void tech()
    {
        if (!atPercent(Protoss_Cybernetics_Core, 1.00))
            return;

        const auto firstTechUnit = !techList.empty() ? *techList.begin() : None;
        const auto skipOneTech = int(firstUnit == None || (firstUnit != None && Stations::getMyStations().size() >= 2) || Strategy::getEnemyBuild() == "FFE" || (Strategy::enemyGasSteal() && !Terrain::isNarrowNatural()));
        const auto techVal = int(techList.size()) + skipOneTech - isTechUnit(Protoss_Shuttle) + isTechUnit(Protoss_Arbiter) - (com(Protoss_Nexus) >= 3 && isTechUnit(Protoss_Dark_Templar));
        techSat = techVal >= int(Stations::getMyStations().size());

        // PvP
        if (Players::PvP()) {
            if (firstTechUnit == Protoss_Dark_Templar)
                techOrder ={ Protoss_High_Templar, Protoss_Observer };
            else
                techOrder ={ Protoss_Reaver, Protoss_High_Templar };
        }

        // PvZ
        if (Players::PvZ()) {
            if (firstTechUnit == Protoss_Reaver)
                techOrder ={ Protoss_Corsair, Protoss_High_Templar };
            else if (firstTechUnit == Protoss_Corsair)
                techOrder ={ Protoss_High_Templar, Protoss_Reaver };
            else if (firstTechUnit == Protoss_High_Templar)
                techOrder ={ Protoss_Corsair, Protoss_Reaver };
            else
                techOrder ={ Protoss_Corsair, Protoss_Observer, Protoss_High_Templar };
        }

        // PvT
        if (Players::PvT()) {
            if (firstTechUnit == Protoss_Dark_Templar)
                techOrder ={ Protoss_Arbiter, Protoss_Observer, Protoss_High_Templar };
            else if (firstTechUnit == Protoss_Carrier)
                techOrder ={ Protoss_Observer, Protoss_High_Templar };
            else
                techOrder ={ Protoss_Observer, Protoss_Arbiter, Protoss_High_Templar };
        }

        // If we have our tech unit, set to none
        if (techComplete())
            techUnit = None;

        // Change desired detection if we get Cannons
        // TODO: Clean up all below this section
        if (Strategy::needDetection() && desiredDetection == Protoss_Forge) {
            buildQueue[Protoss_Forge] = 1;
            buildQueue[Protoss_Photon_Cannon] = com(Protoss_Forge) * 2;

            if (com(Protoss_Photon_Cannon) >= 1) {
                desiredDetection = Protoss_Observer;
                hideTech = true;
            }
        }

        // If production is saturated and none are idle or we need detection, choose a tech
        if ((!inOpeningBook && !getTech && !techSat && techUnit == None) || (Strategy::needDetection() && !isTechUnit(desiredDetection)))
            getTech = true;

        // If we need detection
        if (getTech && Strategy::needDetection() && !isTechUnit(desiredDetection))
            techUnit = desiredDetection;

        // Various hardcoded tech choices
        else if (getTech && currentTransition == "DoubleExpand" && !isTechUnit(Protoss_High_Templar))
            techUnit = Protoss_High_Templar;
        else if (getTech && Strategy::getEnemyTransition() == "4Gate" && !isTechUnit(Protoss_Dark_Templar) && !Strategy::enemyGasSteal())
            techUnit = Protoss_Dark_Templar;

        getNewTech();
        getTechBuildings();
    }

    void situational()
    {
        // Against FFE add a Nexus
        if (Strategy::getEnemyBuild() == "FFE" && Broodwar->getFrameCount() < 15000) {
            auto cannonCount = Players::getVisibleCount(PlayerState::Enemy, Protoss_Photon_Cannon);
            wantNatural = true;

            if (cannonCount < 6) {
                buildQueue[Protoss_Nexus] = 2;
                buildQueue[Protoss_Assimilator] = (vis(Protoss_Nexus) >= 2) + (s >= 120);
                zealotLimit = 0;
                gasLimit = vis(Protoss_Nexus) != buildCount(Protoss_Nexus) ? 0 : INT_MAX;
            }
            else {
                buildQueue[Protoss_Nexus] = 3;
                buildQueue[Protoss_Assimilator] = (vis(Protoss_Nexus) >= 2) + (s >= 120);
                zealotLimit = 0;
                gasLimit = vis(Protoss_Nexus) != buildCount(Protoss_Nexus) ? 0 : INT_MAX;
            }
        }

        // Gas limits
        if ((buildCount(Protoss_Assimilator) == 0 && com(Protoss_Probe) <= 12) || com(Protoss_Probe) <= 8)
            gasLimit = 0;
        else if (com(Protoss_Probe) < 20)
            gasLimit = min(gasLimit, com(Protoss_Probe) / 4);
        else if (!inOpeningBook && com(Protoss_Probe) >= 20)
            gasLimit = INT_MAX;

        // Pylon logic after first two
        if (!inBookSupply) {
            int count = min(22, Players::getSupply(PlayerState::Self) / 14) - (com(Protoss_Nexus) - 1);
            buildQueue[Protoss_Pylon] = count;

            if (com(Protoss_Pylon) >= (Players::vT() ? 5 : 3) || Strategy::getEnemyTransition() == "2HatchMuta" || Strategy::getEnemyTransition() == "3HatchMuta") {
                for (auto &[unit, station] : Stations::getMyStations()) {
                    if (Stations::needPower(*station))
                        buildQueue[Protoss_Pylon] = vis(Protoss_Pylon) + 1;
                }
            }
        }

        // Adding Wall Defenses
        if (Walls::getNaturalWall()) {
            if (vis(Protoss_Forge) > 0 && (Walls::needAirDefenses(*Walls::getNaturalWall()) > 0 || Walls::needGroundDefenses(*Walls::getNaturalWall()) > 0))
                buildQueue[Protoss_Photon_Cannon] = vis(Protoss_Photon_Cannon) + 1;
        }

        // Adding Station Defenses
        if (int(Stations::getMyStations().size()) >= 2) {
            for (auto &station : Stations::getMyStations()) {
                auto &s = *station.second;

                if (vis(Protoss_Forge) > 0 && (Stations::needGroundDefenses(s) > 0 || Stations::needAirDefenses(s) > 0))
                    buildQueue[Protoss_Photon_Cannon] = vis(Protoss_Photon_Cannon) + 1;
            }
        }

        // If we're not in our opener
        if (!inOpeningBook) {
            checkExpand();
            checkRamp();

            // Adding bases
            if (expandDesired)
                buildQueue[Protoss_Nexus] = com(Protoss_Nexus) + 1;

            // Adding production
            auto maxGates = Players::vT() ? 16 : 12;
            auto gatesPerBase = 3.0 - (0.5 * (int(isTechUnit(Protoss_Carrier)) || int(Stations::getMyStations().size()) >= 3));
            productionSat = (vis(Protoss_Gateway) >= int(2.5 * vis(Protoss_Nexus)) || vis(Protoss_Gateway) >= maxGates);
            if (rampDesired) {
                auto gateCount = min({ maxGates, int(round(com(Protoss_Nexus) * gatesPerBase)), vis(Protoss_Gateway) + 1 });
                auto stargateCount = min(4, int(isTechUnit(Protoss_Carrier)) * vis(Protoss_Nexus));
                buildQueue[Protoss_Gateway] = gateCount;
                buildQueue[Protoss_Stargate] = stargateCount;
            }

            // Adding gas
            if (shouldAddGas())
                buildQueue[Protoss_Assimilator] = Resources::getGasCount();

            // Adding upgrade buildings
            if (com(Protoss_Assimilator) >= 3) {
                auto forgeCount = com(Protoss_Assimilator) >= 4 ? 2 - (int)Terrain::isIslandMap() : 1;
                auto coreCount = com(Protoss_Assimilator) >= 4 ? 1 + (int)Terrain::isIslandMap() : 1;

                buildQueue[Protoss_Cybernetics_Core] = 1 + (int)Terrain::isIslandMap();
                buildQueue[Protoss_Forge] = 2 - (int)Terrain::isIslandMap();
            }

            // Add a Forge when playing PvZ
            if (com(Protoss_Nexus) >= 2 && Players::vZ())
                buildQueue[Protoss_Forge] = 1;

            // Ensure we build a core outside our opening book
            if (com(Protoss_Gateway) >= 2)
                buildQueue[Protoss_Cybernetics_Core] = 1;

            // Defensive Cannons
            if (com(Protoss_Forge) >= 1 && ((vis(Protoss_Nexus) >= (Players::vZ() ? 3 : 4)) || (Terrain::isIslandMap() && Players::vZ()))) {
                buildQueue[Protoss_Photon_Cannon] = vis(Protoss_Photon_Cannon);

                for (auto &station : Stations::getMyStations()) {
                    auto &s = *station.second;

                    if (Stations::needGroundDefenses(s) > 0 && !Stations::needPower(s))
                        buildQueue[Protoss_Photon_Cannon] = vis(Protoss_Photon_Cannon) + 1;
                }
            }

            // Corsair/Scout upgrades
            if (Players::getSupply(PlayerState::Self) >= 300 && (isTechUnit(Protoss_Scout) || isTechUnit(Protoss_Corsair)))
                buildQueue[Protoss_Fleet_Beacon] = 1;
        }

        // If we want to wall at the natural but we don't have a wall there, check main
        if (wallNat && !Walls::getNaturalWall() && Walls::getMainWall()) {
            wallNat = false;
            wallMain = true;
        }
    }

    void composition()
    {
        if (inOpeningBook && techList.empty())
            return;

        armyComposition.clear();

        // Ordered sections in reverse tech order such that it only checks the most relevant section first
        if (Players::vP()) {
            if (Stations::getMyStations().size() >= 4) {
                armyComposition[Protoss_Zealot] = 0.50;
                armyComposition[Protoss_Dragoon] = 0.25;
                armyComposition[Protoss_Archon] = 0.25;
            }
            else if (isTechUnit(Protoss_High_Templar)) {
                armyComposition[Protoss_Zealot] = 0.50;
                armyComposition[Protoss_Dragoon] = 0.50;
            }
            else {
                armyComposition[Protoss_Zealot] = 0.05;
                armyComposition[Protoss_Dragoon] = 0.95;
            }
        }

        if (Players::vT()) {
            if (isTechUnit(Protoss_Carrier)) {
                armyComposition[Protoss_Zealot] = 0.25;
                armyComposition[Protoss_Dragoon] = 0.75;
            }
            else if (isTechUnit(Protoss_High_Templar) || isTechUnit(Protoss_Arbiter)) {
                armyComposition[Protoss_Zealot] = 0.40;
                armyComposition[Protoss_Dragoon] = 0.60;
            }
            else
                armyComposition[Protoss_Dragoon] = 1.00;            
        }

        if (Players::vZ()) {
            if (currentTransition == "4Gate" || currentTransition == "5GateGoon")
                armyComposition[Protoss_Dragoon] = 1.00;
            else if (Stations::getMyStations().size() >= 3) {
                armyComposition[Protoss_Zealot] = 0.40;
                armyComposition[Protoss_Dragoon] = 0.20;
                armyComposition[Protoss_Archon] = 0.40;
            }
            else
                armyComposition[Protoss_Zealot] = 1.00;
        }

        for (auto &type : techList)
            armyComposition[type] = 0.05;
    }

    void unlocks()
    {
        // Leg upgrade check
        auto zealotLegs = Broodwar->self()->getUpgradeLevel(UpgradeTypes::Leg_Enhancements) > 0
            || (com(Protoss_Citadel_of_Adun) > 0 && Players::getSupply(PlayerState::Self) >= 200);

        // Check if we should always make Zealots
        if (zealotLimit > vis(Protoss_Zealot)
            || zealotLegs)
            unlockedType.insert(Protoss_Zealot);
        else
            unlockedType.erase(Protoss_Zealot);

        // Check if we should always make Dragoons
        if ((Players::vZ() && Broodwar->getFrameCount() > 20000)
            || Players::getVisibleCount(PlayerState::Enemy, Zerg_Lurker) > 0
            || dragoonLimit > vis(Protoss_Dragoon))
            unlockedType.insert(Protoss_Dragoon);
        else
            unlockedType.erase(Protoss_Dragoon);

        //// Add Observers if we have a Reaver
        //if (vis(Protoss_Reaver) >= 2) {
        //    techList.insert(Protoss_Observer);
        //    unlockedType.insert(Protoss_Observer);
        //}

        //// Add Reavers if we have a Observer in PvP
        //if (Players::vP() && vis(Protoss_Observer) >= 1) {
        //    techList.insert(Protoss_Reaver);
        //    unlockedType.insert(Protoss_Reaver);
        //}

        // Add Shuttles if we have Reavers/HT
        if (com(Protoss_Robotics_Facility) > 0 && (isTechUnit(Protoss_Reaver) || isTechUnit(Protoss_High_Templar) || (Players::vP() && !Strategy::needDetection() && isTechUnit(Protoss_Observer)))) {
            techList.insert(Protoss_Shuttle);
            unlockedType.insert(Protoss_Shuttle);
        }

        // Add DT late game
        if (Stations::getMyStations().size() >= 4) {
            techList.insert(Protoss_Dark_Templar);
            unlockedType.insert(Protoss_Dark_Templar);
        }

        // Add HT or Arbiter if enemy has detection
        if (com(Protoss_Dark_Templar) > 0) {
            auto substitute = Players::vT() ? Protoss_Arbiter : Protoss_High_Templar;
            if (!Players::vP() && Players::hasDetection(PlayerState::Enemy)) {
                unlockedType.insert(substitute);
                techList.insert(substitute);
            }
        }

        // Remove DT if enemy has Observers in PvP
        if (Players::PvP() && total(Protoss_Dark_Templar) >= 4 && Players::getVisibleCount(PlayerState::Enemy, Protoss_Observer) > 0) {
            unlockedType.erase(Protoss_Dark_Templar);
            techList.erase(Protoss_Dark_Templar);
        }
    }

    void island()
    {
        // DISABLED: Islands not being played
        if (rampDesired) {

            // PvZ island
            if (Players::vZ()) {
                int nexusCount = vis(Protoss_Nexus);
                int roboCount = min(nexusCount - 2, vis(Protoss_Robotics_Facility) + 1);
                int stargateCount = min(nexusCount, vis(Protoss_Stargate) + 1);

                if (Broodwar->self()->gas() - Production::getReservedGas() - Buildings::getQueuedGas() > 150) {
                    buildQueue[Protoss_Stargate] = stargateCount;
                    buildQueue[Protoss_Robotics_Facility] = roboCount;
                    buildQueue[Protoss_Robotics_Support_Bay] = 1;
                }
                buildQueue[Protoss_Gateway] = nexusCount;
            }

            // PvP island
            else if (Players::vP()) {
                int nexusCount = vis(Protoss_Nexus);
                int gateCount = min(com(Protoss_Nexus) * 3, vis(Protoss_Gateway) + 1);

                if (Broodwar->self()->gas() - Production::getReservedGas() - Buildings::getQueuedGas() > 200) {
                    buildQueue[Protoss_Robotics_Support_Bay] = 1;
                    if (techList.find(Protoss_Scout) != techList.end() || techList.find(Protoss_Carrier) != techList.end())
                        buildQueue[Protoss_Stargate] = nexusCount;
                }

                buildQueue[Protoss_Gateway] = gateCount;
            }

            // PvT island
            else {
                int nexusCount = vis(Protoss_Nexus);
                int stargateCount = min(nexusCount + 1, vis(Protoss_Stargate) + 1);
                if (Broodwar->self()->gas() - Production::getReservedGas() - Buildings::getQueuedGas() > 150) {
                    buildQueue[Protoss_Stargate] = stargateCount;
                    buildQueue[Protoss_Robotics_Facility] = min(1, stargateCount - 2);
                    buildQueue[Protoss_Robotics_Support_Bay] = 1;
                }
                buildQueue[Protoss_Gateway] = nexusCount;
            }
        }
    }
}