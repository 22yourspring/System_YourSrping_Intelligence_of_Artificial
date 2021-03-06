#include "SYSIA.h"

using namespace BWAPI;
using namespace std;
using namespace UnitTypes;

namespace SYSIA::Players
{
    namespace {
        map <Player, PlayerInfo> thePlayers;
        map <Race, int> raceCount;
        map <PlayerState, map<UnitType, int>> visibleTypeCounts;
        map <PlayerState, map<UnitType, int>> completeTypeCounts;
        map <PlayerState, map<UnitType, int>> totalTypeCounts;

        void update(PlayerInfo& player)
        {
            // Add up the number of each race - HACK: Don't add self for now
            player.update();
            if (!player.isSelf())
                raceCount[player.getCurrentRace()]++;

            auto &visCounts = visibleTypeCounts[player.getPlayerState()];
            auto &comCounts = completeTypeCounts[player.getPlayerState()];

            for (auto &u : player.getUnits()) {
                auto &unit = *u;

                // If unit has a valid type, update enemy composition tracking
                if (unit.getType().isValid())
                    visCounts[unit.getType()] += 1;
                if (unit.getType().isValid() && unit.isCompleted())
                    comCounts[unit.getType()] += 1;
            }
        }
    }

    void onStart()
    {
        // Store all players
        for (auto player : Broodwar->getPlayers()) {
            auto race = player->isNeutral() ? Races::None : player->getRace(); // BWAPI returns Zerg for neutral race

            PlayerInfo &p = thePlayers[player];
            p.setPlayer(player);
            p.setStartRace(race);
            p.update();

            if (!p.isSelf())
                raceCount[p.getCurrentRace()]++;
        }
    }

    void onFrame()
    {
        // Clear race count and recount
        visibleTypeCounts.clear();
        completeTypeCounts.clear();
        raceCount.clear();
        for (auto &[_, player] : thePlayers)
            update(player);
    }

    void storeUnit(Unit bwUnit)
    {
        auto p = getPlayerInfo(bwUnit->getPlayer());
        auto info = make_shared<UnitInfo>(bwUnit);

        if (Units::getUnitInfo(bwUnit))
            return;

        if (bwUnit->getType().isBuilding() && bwUnit->getPlayer() == Broodwar->self()) {
            auto closestWorker = Util::getClosestUnit(bwUnit->getPosition(), PlayerState::Self, [&](auto &u) {
                return u.getRole() == Role::Worker && (u.getType() != Terran_SCV || bwUnit->isCompleted()) && u.getBuildPosition() == bwUnit->getTilePosition();
            });
            if (closestWorker) {
                closestWorker->setBuildingType(None);
                closestWorker->setBuildPosition(TilePositions::Invalid);
            }
        }

        if (p) {

            // Setup the UnitInfo and update
            p->getUnits().insert(info);
            info->update();

            if (info->getPlayer() == Broodwar->self() && info->getType() == Protoss_Pylon)
                Pylons::storePylon(bwUnit);

            // Increase total counts
            if (Broodwar->getFrameCount() == 0 || !p->isSelf() || (info->getType() != Zerg_Zergling && info->getType() != Zerg_Scourge))
                totalTypeCounts[p->getPlayerState()][info->getType()]  += 1;
        }
    }

    void removeUnit(Unit bwUnit)
    {
        BWEB::Map::onUnitDestroy(bwUnit);

        // Find the unit
        for (auto &[_, player] : Players::getPlayers()) {
            for (auto &u : player.getUnits()) {
                if (u->unit() == bwUnit) {

                    // Remove assignments and roles
                    if (u->hasTransport())
                        Transports::removeUnit(*u);
                    if (u->hasResource())
                        Workers::removeUnit(*u);
                    if (u->getRole() == Role::Scout)
                        Scouts::removeUnit(*u);
                    if (u->getPlayer() == Broodwar->self() && u->getType() == Protoss_Pylon)
                        Pylons::removePylon(bwUnit);

                    // Invalidates iterator, must return
                    player.getUnits().erase(u);
                    return;
                }
            }
        }
    }

    void morphUnit(Unit bwUnit)
    {
        auto p = getPlayerInfo(bwUnit->getPlayer());

        // HACK: Changing players is kind of annoying, so we just remove and re-store
        if (bwUnit->getType().isRefinery()) {
            removeUnit(bwUnit);
            storeUnit(bwUnit);
        }

        // Morphing into a Hatchery
        if (bwUnit->getType() == Zerg_Hatchery)
            Stations::storeStation(bwUnit);

        // Grab the UnitInfo for this unit
        auto info = Units::getUnitInfo(bwUnit);
        if (info) {
            if (info->hasResource())
                Workers::removeUnit(*info);
            if (info->hasTransport())
                Transports::removeUnit(*info);
            if (info->hasTarget())
                info->setTarget(nullptr);
            if (info->getRole() == Role::Scout)
                Scouts::removeUnit(*info);

            // When we morph a larva, store the type of what we are making, rather than the egg
            if (p->isSelf() && info->getType() == Zerg_Larva) {
                auto type = bwUnit->getBuildType();
                totalTypeCounts[p->getPlayerState()][type] += 1 + (type == Zerg_Zergling || type == Zerg_Scourge);
                totalTypeCounts[p->getPlayerState()][info->getType()]--;
            }

            // When enemy morphs a larva, store the egg type, when the egg hatches, store the morphed type
            if (!p->isSelf() && (info->getType() == Zerg_Larva || info->getType() == Zerg_Egg)) {
                totalTypeCounts[p->getPlayerState()][bwUnit->getType()] += 1;
                totalTypeCounts[p->getPlayerState()][info->getType()] -= 1;
            }

            // When an existing type morphs again
            if (info->getType() == Zerg_Hydralisk || info->getType() == Zerg_Lurker_Egg || info->getType() == Zerg_Mutalisk || info->getType() == Zerg_Cocoon || info->getType() == Zerg_Creep_Colony) {
                totalTypeCounts[p->getPlayerState()][bwUnit->getType()] += 1;
                totalTypeCounts[p->getPlayerState()][info->getType()] -= 1;
            }

            // When an existing Drone morphs into a building
            if (info->getType() == Zerg_Drone && bwUnit->getType().isBuilding()) {
                totalTypeCounts[p->getPlayerState()][bwUnit->getType()] += 1;
                totalTypeCounts[p->getPlayerState()][info->getType()] -= 1;
            }
            
            info->setBuildingType(None);
            info->setBuildPosition(TilePositions::Invalid);
        }
    }

    int getCompleteCount(PlayerState state, UnitType type)
    {
        // Finds how many of a UnitType this PlayerState currently has completed
        auto &list = completeTypeCounts[state];
        auto itr = list.find(type);
        if (itr != list.end())
            return itr->second;
        return 0;
    }

    int getVisibleCount(PlayerState state, UnitType type)
    {
        // Finds how many of a UnitType this PlayerState currently has visible
        auto &list = visibleTypeCounts[state];
        auto itr = list.find(type);
        if (itr != list.end())
            return itr->second;
        return 0;
    }

    int getTotalCount(PlayerState state, UnitType type)
    {
        // Finds how many of a UnitType the PlayerState total has ever had
        auto &list = totalTypeCounts[state];
        auto itr = list.find(type);
        if (itr != list.end())
            return itr->second;
        return 0;
    }

    bool hasDetection(PlayerState state)
    {
        return getTotalCount(state, Protoss_Observer) > 0
            || getTotalCount(state, Protoss_Photon_Cannon) > 0
            || getTotalCount(state, Terran_Science_Vessel) > 0
            || getTotalCount(state, Terran_Missile_Turret) > 0
            || getTotalCount(state, Zerg_Overlord) > 0;
    }

    int getSupply(PlayerState state)
    {
        auto combined = 0;
        for (auto &[_, player] : thePlayers) {
            if (player.getPlayerState() == state)
                combined += player.getSupply();
        }
        return combined;
    }

    int getRaceCount(Race race, PlayerState state)
    {
        auto combined = 0;
        for (auto &[_, player] : thePlayers) {
            if (player.getCurrentRace() == race && player.getPlayerState() == state)
                combined += 1;
        }
        return combined;
    }

    Strength getStrength(PlayerState state)
    {
        Strength combined;
        for (auto &[_, player] : thePlayers) {
            if (player.getPlayerState() == state)
                combined += player.getStrength();
        }
        return combined;
    }

    PlayerInfo * getPlayerInfo(Player player)
    {
        for (auto &[p, info] : thePlayers) {
            if (p == player)
                return &info;
        }
        return nullptr;
    }

    map <Player, PlayerInfo>& getPlayers() { return thePlayers; }
    bool vP() { return (thePlayers.size() == 3 && raceCount[Races::Protoss] > 0); }
    bool vT() { return (thePlayers.size() == 3 && raceCount[Races::Terran] > 0); }
    bool vZ() { return (thePlayers.size() == 3 && raceCount[Races::Zerg] > 0); }
}