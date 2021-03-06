#include "SYSIA.h"

using namespace BWAPI;
using namespace std;
using namespace UnitTypes;

namespace SYSIA::Combat {

    namespace {

        int lastCheckFrame = 0;
        weak_ptr<UnitInfo> checker;

        int lastRoleChange = 0;
        bool clusterTooSpread = false;
        set<Position> retreatPositions;
        set<Position> defendPositions;
        vector<Position> lastSimPositions;
        multimap<double, Position> combatClusters;
        map<const BWEM::ChokePoint*, vector<WalkPosition>> concaveCache;

        BWEB::Path airClusterPath;
        pair<double, Position> airCluster;
        pair<UnitCommandType, Position> airCommanderCommand;
        multimap<double, Position> groundCleanupPositions;
        multimap<double, Position> airCleanupPositions;
        map<TilePosition, int> surroundTileClaimers;

        constexpr tuple commands{ Command::misc, Command::special, Command::attack, Command::approach, Command::kite, Command::defend, Command::explore, Command::escort, Command::retreat, Command::move };

        Position findConcavePosition(UnitInfo& unit, BWEM::Area const * area, BWEM::ChokePoint const * choke)
        {
            // Force ranged concaves if enemy has ranged units (defending only)
            const auto enemyRangeExists = Players::getTotalCount(PlayerState::Enemy, UnitTypes::Protoss_Dragoon) > 0
                || Players::getTotalCount(PlayerState::Enemy, UnitTypes::Zerg_Hydralisk) > 0
                || Players::vT();

            // Don't try concaves without chokepoints for now (can use lines in future)
            if (!choke)
                return unit.getPosition();

            // Push defense position away when trying to build defenses
            auto closestBuilder = Util::getClosestUnit(unit.getPosition(), PlayerState::Self, [&](auto &u) {
                return u.getBuildPosition().isValid();
            });

            auto chokeCount = area->ChokePoints().size();
            auto chokeCenter = Position(choke->Center());
            auto isMelee = unit.getGroundDamage() > 0 && unit.getGroundRange() <= 32.0;
            auto base = area->Bases().empty() ? nullptr : &area->Bases().front();
            auto scoreBest = 0.0;
            auto posBest = unit.getPosition();

            auto useMeleeRadius = isMelee && !enemyRangeExists && Players::getSupply(PlayerState::Self) < 80 && !Players::ZvT();
            auto radius = clamp(vis(unit.getType()) * unit.getType().width() / 2.0, (choke->Width() / 2.0), (choke->Width() * 2.0)) + (unit.getGroundRange() - 32.0);
            auto alreadyValid = false;

            // Choke end nodes and distance to choke center
            auto p1 = Position(choke->Pos(choke->end1));
            auto p2 = Position(choke->Pos(choke->end2));
            auto p1Dist = p1.getDistance(chokeCenter);
            auto p2Dist = p2.getDistance(chokeCenter);

            const auto isValid = [&](WalkPosition w, Position projection) {
                const auto t = TilePosition(w);
                const auto p = Position(w);

                if (!w.isValid()
                    || (alreadyValid && p.getDistance(unit.getPosition()) > 160.0)
                    || (closestBuilder && p.getDistance(closestBuilder->getPosition()) < 64.0)
                    || p.getDistance(projection) < radius - 32.0
                    || p.getDistance(projection) >= radius + 32.0
                    || Buildings::overlapsPlan(unit, p)
                    || !Broodwar->isWalkable(w)
                    || !Util::isTightWalkable(unit, p)
                    || Actions::overlapsActions(unit.unit(), p, TechTypes::Spider_Mines, PlayerState::Enemy, 96))
                    return false;
                return true;
            };

            const auto scorePosition = [&](WalkPosition w, Position projection) {
                const auto p = Position(w);

                const auto distProj = exp(p.getDistance(projection));
                const auto distCenter = p.getDistance(chokeCenter);
                const auto distUnit = p.getDistance(unit.getPosition());
                const auto distAreaBase = base ? base->Center().getDistance(p) : 1.0;
                return 1.0 / (distCenter * distAreaBase * distUnit * distProj);
            };

            auto currentProjection = Util::vectorProjection(make_pair(p1, p2), Position(unit.getWalkPosition()));
            if (unit.unit()->getOrder() == Orders::HoldPosition && isValid(unit.getWalkPosition(), currentProjection))
                return unit.getPosition();

            // Find a position around the center that is suitable        
            auto &tiles = concaveCache[choke];
            for (auto &w : tiles) {
                auto projection = Util::vectorProjection(make_pair(p1, p2), Position(w));

                // Find a vector projection of this point
                auto projDist = projection.getDistance(chokeCenter);

                // Determine if we should lineup at projection or wrap around choke end nodes
                if (chokeCount < 3 && (p1Dist < projDist || p2Dist < projDist))
                    projection = (p1.getDistance(projection) < p2.getDistance(projection)) ? p1 : p2;

                const auto score = scorePosition(w, projection);

                // Swap order after we done debugging
                if (score > scoreBest && isValid(w, projection)) {
                    posBest = Position(w);
                    scoreBest = score;
                }
            }
            return posBest;
        }

        void updateRole(UnitInfo& unit)
        {
            // Can't change role to combat if not a worker or we did one this frame
            if (!unit.getType().isWorker()
                || lastRoleChange == Broodwar->getFrameCount())
                return;

            // Only proactively pull the closest worker to our defend position
            auto closestWorker = Util::getClosestUnit(Terrain::getDefendPosition(), PlayerState::Self, [&](auto &u) {
                return u.getRole() == Role::Worker && (!unit.hasResource() || !unit.getResource().getType().isRefinery()) && !unit.getBuildPosition().isValid();
            });

            auto combatCount = Units::getMyRoleCount(Role::Combat) - (unit.getRole() == Role::Combat ? 1 : 0);
            auto combatWorkersCount =  Units::getMyRoleCount(Role::Combat) - com(Protoss_Zealot) - com(Protoss_Dragoon) - com(Zerg_Zergling) - (unit.getRole() == Role::Combat ? 1 : 0);

            const auto healthyWorker = [&] {

                // Don't pull low shield probes
                if (unit.getType() == Protoss_Probe && unit.getShields() <= 4)
                    return false;

                // Don't pull low health drones
                if (unit.getType() == Zerg_Drone && unit.getHealth() < 20)
                    return false;
                return true;
            };

            // Proactive pulls will result in the worker defending
            const auto proactivePullWorker = [&]() {

                // If this isn't the closest mineral worker to the defend position, don't pull it
                if (unit.getRole() == Role::Worker && unit.shared_from_this() != closestWorker)
                    return false;

                // Protoss
                if (Broodwar->self()->getRace() == Races::Protoss) {
                    int completedDefenders = com(Protoss_Photon_Cannon) + com(Protoss_Zealot);
                    int visibleDefenders = vis(Protoss_Photon_Cannon) + vis(Protoss_Zealot);

                    //// Don't pull workers too early
                    //if (Strategy::enemyArrivalTime() < Time(2, 00) && arriveAtDefense < Strategy::enemyArrivalTime() - Time(0, 30))
                    //    return false;

                    // If trying to hide tech, pull 1 probe with a Zealot
                    if (!BuildOrder::isRush() && BuildOrder::isHideTech() && combatCount < 2 && completedDefenders > 0)
                        return true;

                    // If trying to FFE, pull based on Cannon/Zealot numbers, or lack of scouting information
                    if (BuildOrder::getCurrentBuild() == "FFE") {
                        if (Strategy::enemyRush() && Strategy::getEnemyOpener() == "4Pool" && combatCount < 8 - (2 * completedDefenders) && visibleDefenders >= 1)
                            return true;
                        if (Strategy::enemyPressure() && combatCount < 8 - (2 * completedDefenders) && visibleDefenders >= 2)
                            return true;
                        if (Strategy::enemyRush() && Strategy::getEnemyOpener() == "9Pool" && Util::getTime() > Time(3, 15) && completedDefenders < 3)
                            return combatWorkersCount < 3;
                        if (!Terrain::getEnemyStartingPosition().isValid() && Strategy::getEnemyBuild() == "Unknown" && combatCount < 2 && completedDefenders < 1 && visibleDefenders > 0)
                            return true;
                    }

                    // If trying to 2Gate at our natural, pull based on Zealot numbers
                    else if (BuildOrder::getCurrentBuild() == "2Gate" && BuildOrder::getCurrentOpener() == "Natural") {
                        if (Strategy::enemyRush() && combatCount < 8 - (2 * completedDefenders) && visibleDefenders >= 1)
                            return true;
                        if (Strategy::enemyPressure() && combatCount < 8 - (2 * completedDefenders) && visibleDefenders >= 2)
                            return true;
                    }

                    // If trying to 1GateCore and scouted 2Gate late, pull workers to block choke when we are ready
                    else if (BuildOrder::getCurrentBuild() == "1GateCore" && Strategy::getEnemyBuild() == "2Gate" && BuildOrder::getCurrentTransition() != "Defensive" && Strategy::defendChoke()) {
                        if (Util::getTime() < Time(3, 30) && combatWorkersCount < 2)
                            return true;
                    }
                }

                // Terran

                // Zerg
                if (Broodwar->self()->getRace() == Races::Zerg) {
                    if (BuildOrder::getCurrentOpener() == "12Pool" && Strategy::getEnemyOpener() == "9Pool" && total(Zerg_Zergling) < 16)
                        return combatWorkersCount < 3;
                }
                return false;
            };

            // Reactive pulls will cause the worker to attack aggresively
            const auto reactivePullWorker = [&]() {

                auto proxyDangerousBuilding = Util::getClosestUnit(BWEB::Map::getMainPosition(), PlayerState::Enemy, [&](auto &u) {
                    return u.isProxy() && u.getType().isBuilding() && u.canAttackGround();
                });
                auto proxyBuildingWorker = Util::getClosestUnit(BWEB::Map::getMainPosition(), PlayerState::Enemy, [&](auto &u) {
                    return u.getType().isWorker() && (u.isThreatening() || (proxyDangerousBuilding && u.getType().isWorker() && u.getPosition().getDistance(proxyDangerousBuilding->getPosition()) < 160.0));
                });    

                // HACK: Don't pull workers reactively versus vultures
                if (Players::getVisibleCount(PlayerState::Enemy, Terran_Vulture) > 0)
                    return false;
                if (Strategy::getEnemyBuild() == "2Gate" && Strategy::enemyProxy())
                    return false;

                // If we have immediate threats
                if (Players::ZvT() && proxyDangerousBuilding && com(Zerg_Zergling) <= 2)
                    return combatWorkersCount < 6;
                if (Players::ZvP() && (proxyBuildingWorker || proxyDangerousBuilding) && Strategy::getEnemyBuild() == "CannonRush" && com(Zerg_Zergling) <= 2)
                    return combatWorkersCount < (4 * Players::getVisibleCount(PlayerState::Enemy, proxyDangerousBuilding->getType()));
                if (Strategy::getWorkersNearUs() > 2 && Strategy::getWorkersNearUs() >= combatWorkersCount)
                    return Strategy::getWorkersNearUs() >= combatWorkersCount - 1;
                if (BuildOrder::getCurrentOpener() == "12Hatch" && Strategy::getEnemyOpener() == "8Rax" && com(Zerg_Zergling) < 2)
                    return combatWorkersCount <= com(Zerg_Drone) - 4;

                // If we're trying to make our expanding hatchery and the drone is being harassed
                if (vis(Zerg_Hatchery) == 1 && Util::getTime() < Time(3, 00) && BuildOrder::isOpener() && Units::getImmThreat() > 0.0f && Players::ZvP() && combatCount == 0)
                    return true;
                return false;
            };

            // Check if workers should fight or work
            if (unit.getType().isWorker()) {
                auto react = reactivePullWorker();
                auto proact = proactivePullWorker();

                // Pull a worker if needed
                if (unit.getRole() == Role::Worker && !unit.unit()->isCarryingMinerals() && !unit.unit()->isCarryingGas() && healthyWorker() && (react || proact)) {
                    unit.setRole(Role::Combat);
                    unit.setBuildingType(None);
                    unit.setBuildPosition(TilePositions::Invalid);
                    lastRoleChange = Broodwar->getFrameCount();
                }

                // Return a worker if not needed
                else if (unit.getRole() == Role::Combat && ((!react && !proact) || !healthyWorker())) {
                    unit.setRole(Role::Worker);
                    lastRoleChange = Broodwar->getFrameCount();
                }

                // HACK: Check if this was a reactive pull, set worker to always engage
                if (unit.getRole() == Role::Combat) {
                    combatCount--;
                    combatWorkersCount--;
                    react = reactivePullWorker();
                    if (react) {
                        unit.setLocalState(LocalState::Attack);
                        unit.setGlobalState(GlobalState::Attack);
                    }
                }
            }
        }

        void updateClusters(UnitInfo& unit)
        {
            // Don't update clusters for fragile combat units
            if (unit.getType() == Protoss_High_Templar
                || unit.getType() == Protoss_Dark_Archon
                || unit.getType() == Protoss_Reaver
                || unit.getType() == Protoss_Interceptor
                || unit.getType() == Zerg_Defiler)
                return;

            // Figure out what type to make the center of our cluster around
            auto clusterAround = UnitTypes::None;
            if (Broodwar->self()->getRace() == Races::Protoss)
                clusterAround = vis(Protoss_Carrier) > 0 ? Protoss_Carrier : Protoss_Corsair;
            else if (Broodwar->self()->getRace() == Races::Zerg)
                clusterAround = vis(Zerg_Guardian) > 0 ? Zerg_Guardian : Zerg_Mutalisk;
            else if (Broodwar->self()->getRace() == Races::Terran)
                clusterAround = vis(Terran_Battlecruiser) > 0 ? Terran_Battlecruiser : Terran_Wraith;

            if (unit.isFlying() && unit.getType() == clusterAround) {
                if (Grids::getAAirCluster(unit.getWalkPosition()) > airCluster.first)
                    airCluster = make_pair(Grids::getAAirCluster(unit.getWalkPosition()), unit.getPosition());
            }
            else if (!unit.isFlying()) {
                const auto strength = Grids::getAGroundCluster(unit.getWalkPosition()) + Grids::getAAirCluster(unit.getWalkPosition());
                combatClusters.emplace(strength, unit.getPosition());
            }
        }

        void updateLocalState(UnitInfo& unit)
        {
            if (!unit.hasTarget()) {
                unit.setLocalState(LocalState::None);
                return;
            }

            bool testingAttack = false;
            if (testingAttack) {
                unit.setLocalState(LocalState::Attack);
                unit.setGlobalState(GlobalState::Attack);
                return;
            }

            if (unit.getLocalState() != LocalState::None)
                return;

            const auto simRadius = (unit.getGoal().isValid() && unit.getType() == Zerg_Zergling && unit.getPosition().getDistance(unit.getGoal()) > unit.getSimRadius()) ? unit.getSimRadius() - 96.0 : unit.getSimRadius();
            const auto closeToSim = double(Util::boxDistance(unit.getType(), unit.getPosition(), unit.getSimTarget().getType(), unit.getSimTarget().getPosition())) < simRadius || Terrain::isInAllyTerritory(unit.getTarget().getTilePosition());
            const auto temporaryRetreat = unit.isLightAir() && unit.hasTarget() && unit.canStartAttack() && !unit.isWithinAngle(unit.getTarget()) && Util::boxDistance(unit.getType(), unit.getPosition(), unit.getTarget().getType(), unit.getTarget().getPosition()) <= 16.0;

            if (!checker.expired() && checker.lock() && unit.unit() == checker.lock()->unit())
                unit.setLocalState(LocalState::None);

            // Regardless of any decision, determine if Unit is in danger and needs to retreat
            else if (Actions::isInDanger(unit, unit.getPosition())
                || (Actions::isInDanger(unit, unit.getEngagePosition()) && unit.getPosition().getDistance(unit.getEngagePosition()) < simRadius)
                || temporaryRetreat)
                unit.setLocalState(LocalState::Retreat);

            // Regardless of local decision, determine if Unit needs to attack or retreat
            else if (unit.globalEngage())
                unit.setLocalState(LocalState::Attack);            
            else if (unit.globalRetreat())
                unit.setLocalState(LocalState::Retreat);

            // If within local decision range, determine if Unit needs to attack or retreat
            else if (closeToSim) {
                if (unit.localRetreat() || unit.getSimState() == SimState::Loss)
                    unit.setLocalState(LocalState::Retreat);
                else if (unit.localEngage() || unit.getSimState() == SimState::Win)
                    unit.setLocalState(LocalState::Attack);
            }

            // Default state
            else
                unit.setLocalState(LocalState::None);
        }

        void updateGlobalState(UnitInfo& unit)
        {
            bool testingDefense = false;
            if (testingDefense) {
                unit.setGlobalState(GlobalState::Retreat);
                return;
            }

            if (unit.getGlobalState() != GlobalState::None)
                return;

            // Protoss
            if (Broodwar->self()->getRace() == Races::Protoss) {
                if ((!BuildOrder::takeNatural() && Strategy::enemyFastExpand())
                    || (Strategy::enemyProxy() && !Strategy::enemyRush())
                    || BuildOrder::isRush()
                    || unit.getType() == Protoss_Dark_Templar
                    || (Players::getVisibleCount(PlayerState::Enemy, Protoss_Dark_Templar) > 0 && com(Protoss_Observer) == 0 && Broodwar->getFrameCount() < 15000))
                    unit.setGlobalState(GlobalState::Attack);

                else if (unit.getType().isWorker()
                    || (Broodwar->getFrameCount() < 15000 && BuildOrder::isPlayPassive())
                    || (unit.getType() == Protoss_Corsair && !BuildOrder::firstReady() && Players::getStrength(PlayerState::Enemy).airToAir > 0.0)
                    || (unit.getType() == Protoss_Carrier && com(Protoss_Interceptor) < 16 && !Strategy::enemyPressure()))
                    unit.setGlobalState(GlobalState::Retreat);
                else
                    unit.setGlobalState(GlobalState::Attack);
            }

            // Zerg
            else if (Broodwar->self()->getRace() == Races::Zerg) {

                // Check if we have units outside our base
                auto closestStrangler = Util::getFurthestUnit(BWEB::Map::getMainPosition(), PlayerState::Self, [&](auto &u) {
                    return u.getRole() == Role::Combat && !Terrain::isInAllyTerritory(u.getTilePosition()) && u.hasTarget() && u.getTarget().frameArrivesWhen() < u.frameArrivesWhen();
                });

                if (BuildOrder::isRush())
                    unit.setGlobalState(GlobalState::Attack);
                else if ((Broodwar->getFrameCount() < 15000 && BuildOrder::isPlayPassive() && (!closestStrangler || (!checker.expired() && closestStrangler == checker.lock())))
                    || (Players::ZvT() && Util::getTime() < Time(10, 00) && unit.getType() == Zerg_Zergling && (Strategy::getEnemyBuild() == "RaxFact" || Strategy::enemyWalled() || Players::getCompleteCount(PlayerState::Enemy, Terran_Vulture) > 0))
                    || (Players::ZvZ() && Util::getTime() < Time(10, 00) && unit.getType() == Zerg_Zergling && Players::getCompleteCount(PlayerState::Enemy, Zerg_Zergling) > com(Zerg_Zergling))
                    || (Players::ZvZ() && Players::getCompleteCount(PlayerState::Enemy, Zerg_Drone) > 0 && !Terrain::getEnemyStartingPosition().isValid() && Util::getTime() < Time(2, 45)))
                    unit.setGlobalState(GlobalState::Retreat);
                else
                    unit.setGlobalState(GlobalState::Attack);
            }

            // Terran
            else if (Broodwar->self()->getRace() == Races::Terran) {
                if (BuildOrder::isPlayPassive() || !BuildOrder::firstReady())
                    unit.setGlobalState(GlobalState::Retreat);
                else
                    unit.setGlobalState(GlobalState::Attack);
            }
        }

        void updateDestination(UnitInfo& unit)
        {
            // If attacking and target is close, set as destination
            if (unit.getLocalState() == LocalState::Attack) {
                const auto &intercept = Util::getInterceptPosition(unit);
                if (intercept.getDistance(unit.getTarget().getPosition()) < intercept.getDistance(unit.getPosition()) - 16.0 && (Grids::getMobility(intercept) > 0 || unit.isFlying())) {
                    unit.setDestination(intercept);
                    Broodwar->drawLineMap(unit.getPosition(), unit.getDestination(), Colors::Yellow);
                }
                else {
                    if (surroundTileClaimers[TilePosition(unit.getEngagePosition())] >= 1) {
                        unit.setDestination(unit.getEngagePosition());
                        auto distBest = DBL_MAX;
                        vector<TilePosition> dirs ={ {-1,-1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1} };
                        for (auto t : dirs) {
                            auto tile = t + TilePosition(unit.getEngagePosition());
                            auto center = Position(tile) + Position(16, 16);
                            auto dist = center.getDistance(unit.getPosition());
                            if (dist < distBest && surroundTileClaimers[tile] < 2) {
                                dist = distBest;
                                unit.setDestination(center);
                            }
                        }
                        surroundTileClaimers[TilePosition(unit.getEngagePosition())]++;
                    }
                    else {
                        unit.setDestination(unit.getEngagePosition());
                        surroundTileClaimers[TilePosition(unit.getEngagePosition())]++;
                    }
                    Broodwar->drawLineMap(unit.getPosition(), unit.getDestination(), Colors::Cyan);
                }

                if (unit.getTargetPath().isReachable())
                    unit.setDestinationPath(unit.getTargetPath());
            }

            // If we're globally retreating, set defend position as destination
            else if ((unit.getGlobalState() == GlobalState::Retreat || unit.getGoalType() == GoalType::Defend) && Strategy::defendChoke()) {
                if (unit.getGoal().isValid()) {
                    auto area = mapBWEM.GetArea(TilePosition(unit.getGoal()));
                    auto station = Stations::getClosestStation(PlayerState::Self, unit.getGoal());
                    auto choke = station ? station->getChokepoint() : nullptr;

                    Broodwar->drawLineMap(unit.getPosition(), unit.getGoal(), Colors::Green);

                    if (area && choke)
                        unit.setDestination(findConcavePosition(unit, area, choke));
                    else
                        unit.setDestination(unit.getGoal());
                }
                else {
                    unit.setDestination(findConcavePosition(unit, Terrain::getDefendArea(), Terrain::getDefendChoke()));
                    defendPositions.insert(unit.getDestination());
                }
            }

            // If retreating, find closest retreat position
            else if (unit.getLocalState() == LocalState::Retreat || unit.getGlobalState() == GlobalState::Retreat) {
                const auto &retreat = getClosestRetreatPosition(unit);
                if (retreat.isValid() && (!unit.isLightAir() || Players::getStrength(PlayerState::Enemy).airToAir > 0.0))
                    unit.setDestination(retreat);
                else
                    unit.setDestination(BWEB::Map::getMainPosition());
            }

            // If unit has a goal
            else if (unit.getGoal().isValid()) {
                unit.setDestination(unit.getGoal());
                unit.circleBlue();
            }

            // If this is a light air unit, go to the air cluster first if far away
            else if ((unit.isLightAir() || unit.getType() == Zerg_Scourge) && airCluster.second.isValid() && (unit.getPosition().getDistance(airCluster.second) > 32.0 || clusterTooSpread))
                unit.setDestination(airCluster.second);

            // If this is a light air unit, defend any bases under attack
            else if ((unit.isLightAir() || unit.getType() == Zerg_Scourge) && Units::getImmThreat() > 0.0 &&  Stations::getMyStations().size() >= 3 && Stations::getMyStations().size() > Stations::getEnemyStations().size()) {
                auto &attacker = Util::getClosestUnit(BWEB::Map::getMainPosition(), PlayerState::Enemy, [&](auto &u) {
                    return u.isThreatening();
                });
                if (attacker)
                    unit.setDestination(attacker->getPosition());
            }

            // If this is a light air unit and we can harass
            else if ((unit.isLightAir() || unit.getType() == Zerg_Scourge) && Players::getStrength(PlayerState::Enemy).airToAir * 2 <= Players::getStrength(PlayerState::Self).airToAir) {
                unit.setDestination(Terrain::getHarassPosition());
                unit.setDestinationPath(airClusterPath);
            }

            // If unit has a target and a valid engagement position
            else if (unit.hasTarget()) {
                unit.setDestination(unit.getTarget().getPosition());
                unit.setDestinationPath(unit.getTargetPath());
            }

            // If attack position is valid
            else if (Terrain::getAttackPosition().isValid() && unit.canAttackGround())
                unit.setDestination(Terrain::getAttackPosition());

            // If no target and no enemy bases, move to a base location
            else if (!unit.hasTarget() || !unit.getTarget().getPosition().isValid() || unit.unit()->isIdle()) {

                // Sort unexplored starts by distance
                multimap<double, Position> startsByDist;
                auto basesExplored = 0;
                for (auto &topLeft : mapBWEM.StartingLocations()) {
                    const auto botRight = topLeft + TilePosition(3, 2);
                    const auto center = Position(topLeft) + Position(64, 48);
                    const auto closestScout = Util::getClosestUnit(center, PlayerState::Self, [&](auto &u) {
                        return u.getRole() == Role::Scout && (u.getDestination() == Position(topLeft) || u.getDestination() == Position(botRight));
                    });
                    const auto dist = closestScout ? closestScout->getPosition().getDistance(center) : BWEB::Map::getGroundDistance(center, BWEB::Map::getMainPosition());

                    if (!Broodwar->isExplored(topLeft))
                        startsByDist.emplace(dist, topLeft);
                    else if (!Broodwar->isExplored(botRight))
                        startsByDist.emplace(dist, botRight);
                    else
                        basesExplored++;
                }

                // Scouting for enemy base initially
                if (!startsByDist.empty()) {

                    // Assign closest that isn't assigned to a scout
                    auto avoidFirstScout = (int(Broodwar->getStartLocations().size()) == 4 && basesExplored < 2) || (int(Broodwar->getStartLocations().size()) == 3 && basesExplored < 1);
                    for (auto &[_, position] : startsByDist) {

                        if (avoidFirstScout) {
                            avoidFirstScout = false;
                            continue;
                        }

                        if (!avoidFirstScout || (!Actions::overlapsActions(unit.unit(), position, Broodwar->self()->getRace().getWorker(), PlayerState::Self) && !Actions::overlapsActions(unit.unit(), position, Zerg_Overlord, PlayerState::Self))) {
                            unit.setDestination(position);
                            break;
                        }
                        avoidFirstScout = false;
                    }
                }

                // Finishing enemy off, find remaining bases we haven't scouted
                else if (Terrain::getEnemyStartingPosition().isValid()) {
                    auto best = DBL_MAX;
                    for (auto &area : mapBWEM.Areas()) {
                        for (auto &base : area.Bases()) {
                            if (area.AccessibleNeighbours().size() == 0
                                || Terrain::isInAllyTerritory(base.Location()))
                                continue;

                            int time = Grids::lastVisibleFrame(base.Location());
                            if (time < best) {
                                best = time;
                                unit.setDestination(Position(base.Location()));
                            }
                        }
                    }
                }

                // Finish off positions that are old
                else if (unit.isFlying() && !airCleanupPositions.empty()) {
                    unit.setDestination(airCleanupPositions.begin()->second);
                    airCleanupPositions.erase(airCleanupPositions.begin());
                }
                else if (!unit.isFlying() && !groundCleanupPositions.empty()) {
                    unit.setDestination(groundCleanupPositions.begin()->second);
                    groundCleanupPositions.erase(groundCleanupPositions.begin());
                }
            }

            // Add action so other units dont move to same location
            if (unit.getDestination().isValid())
                Actions::addAction(unit.unit(), unit.getDestination(), None, PlayerState::Self);

            if (unit.unit()->isSelected())
                Broodwar->drawLineMap(unit.getPosition(), unit.getDestination(), Colors::Cyan);
        }

        void updatePath(UnitInfo& unit)
        {
            const auto farAway = unit.getPosition().getDistance(airCluster.second) > 160.0;

            const auto flyerAttack = [&](const TilePosition &t) {
                const auto center = Position(t) + Position(16, 16);
                const auto centerBoxDistance = double(Util::boxDistance(unit.getType(), center, unit.getSimTarget().getType(), unit.getSimTarget().getPosition()));
                const auto unitBoxDistance = double(Util::boxDistance(unit.getType(), unit.getPosition(), unit.getSimTarget().getType(), unit.getSimTarget().getPosition()));
                if (farAway)
                    return (t.x > 1 && t.y > 1 && t.x < Broodwar->mapWidth() - 1 && t.y < Broodwar->mapHeight() - 1)
                    && centerBoxDistance >= unit.getSimRadius();


                auto noneTooClose = true;
                for (auto &pos : lastSimPositions) {
                    auto posToUnit = Util::boxDistance(unit.getType(), unit.getPosition(), unit.getSimTarget().getType(), pos);
                    auto posToCenter = Util::boxDistance(unit.getType(), center, unit.getSimTarget().getType(), pos);
                    if (posToUnit >= unit.getSimRadius() - 48.0 && posToCenter <= unit.getSimRadius() + 48.0) {
                        noneTooClose = false;
                        break;
                    }
                }

                return (t.x > 1 && t.y > 1 && t.x < Broodwar->mapWidth() - 1 && t.y < Broodwar->mapHeight() - 1)
                    && (Broodwar->getFrameCount() - Grids::lastVisibleFrame(t) > 250 || noneTooClose || (unit.hasTarget() && (unit.localEngage() || unit.getSimState() == SimState::Win || unit.globalEngage()) && centerBoxDistance < unitBoxDistance));
            };

            const auto flyerRetreat = [&](const TilePosition &t) {
                const auto center = Position(t) + Position(16, 16);
                const auto centerBoxDistance = double(Util::boxDistance(unit.getType(), center, unit.getSimTarget().getType(), unit.getSimTarget().getPosition()));
                const auto unitBoxDistance = double(Util::boxDistance(unit.getType(), unit.getPosition(), unit.getSimTarget().getType(), unit.getSimTarget().getPosition()));
                return (Grids::getEAirThreat(center) <= 0.0 || centerBoxDistance > unitBoxDistance);
            };

            BWEB::Pathfinding::clearCache(flyerAttack); // No caching right now for flying paths
            BWEB::Pathfinding::clearCache(flyerRetreat); // No caching right now for flying paths

            // Generate a new path that obeys collision of terrain and buildings
            if (!unit.isFlying() && unit.getDestinationPath() != unit.getTargetPath() && unit.getDestinationPath().getTarget() != TilePosition(unit.getDestination())) {
                BWEB::Path newPath(unit.getPosition(), unit.getDestination(), unit.getType());
                if (newPath.unitWalkable(TilePosition(unit.getDestination()))) {
                    newPath.generateJPS([&](const TilePosition &t) { return newPath.unitWalkable(t); });
                    unit.setDestinationPath(newPath);
                }
            }

            // Generate a flying path for harassing that obeys exploration and staying out of range of threats if possible
            auto canHarass = unit.hasSimTarget() && (!Players::ZvZ() || (Players::getStrength(PlayerState::Enemy).airToAir <= 0.0f && Strategy::getEnemyTransition().find("Muta") == string::npos));
            if (unit.isLightAir() && canHarass) {
                if (unit.getLocalState() != LocalState::Retreat && (unit.getDestination() == Terrain::getHarassPosition() || farAway)) {
                    BWEB::Path newPath(unit.getPosition(), unit.getDestination(), unit.getType());
                    newPath.generateJPS(flyerAttack);
                    unit.setDestinationPath(newPath);
                    Visuals::displayPath(newPath);
                }
            }

            // If path is reachable, find a point n pixels away to set as new destination
            if (unit.getDestinationPath().isReachable()) {
                auto newDestination = Util::findPointOnPath(unit.getDestinationPath(), [&](Position p) {
                    return p.getDistance(unit.getPosition()) >= 64.0;
                });

                if (newDestination.isValid())
                    unit.setDestination(newDestination);
            }

            // If not reachable, use a point along a BWEM Path
            else if (!unit.isFlying() && unit.getPosition().isValid() && unit.getDestination().isValid() && mapBWEM.GetArea(TilePosition(unit.getPosition())) && mapBWEM.GetArea(TilePosition(unit.getDestination()))) {
                for (auto &choke : mapBWEM.GetPath(unit.getPosition(), unit.getDestination())) {
                    auto center = Position(choke->Center());
                    if (center.getDistance(unit.getPosition()) > 64.0) {
                        unit.setDestination(center);
                        break;
                    }
                }
            }
        }

        void updateDecision(UnitInfo& unit)
        {
            if (!unit.unit() || !unit.unit()->exists()                                                                                            // Prevent crashes            
                || unit.unit()->isLoaded()
                || unit.unit()->isLockedDown() || unit.unit()->isMaelstrommed() || unit.unit()->isStasised() || !unit.unit()->isCompleted())    // If the unit is locked down, maelstrommed, stassised, or not completed
                return;

            // Convert our commands to strings to display what the unit is doing for debugging
            map<int, string> commandNames{
                make_pair(0, "Misc"),
                make_pair(1, "Special"),
                make_pair(2, "Attack"),
                make_pair(3, "Approach"),
                make_pair(4, "Kite"),
                make_pair(5, "Defend"),
                make_pair(6, "Explore"),
                make_pair(7, "Escort"),
                make_pair(8, "Retreat"),
                make_pair(9, "Move")
            };

            // Iterate commands, if one is executed then don't try to execute other commands
            int height = unit.getType().height() / 2;
            int width = unit.getType().width() / 2;
            int i = Util::iterateCommands(commands, unit);
            auto startText = unit.getPosition() + Position(-4 * int(commandNames[i].length() / 2), height);
            Broodwar->drawTextMap(startText, "%c%s", Text::White, commandNames[i].c_str());
        }

        void updateCleanup()
        {
            groundCleanupPositions.clear();
            airCleanupPositions.clear();

            if (Util::getTime() < Time(6, 00) || !Stations::getEnemyStations().empty())
                return;

            // Look at every TilePosition and sort by furthest oldest
            auto best = 0.0;
            for (int x = 0; x < Broodwar->mapWidth(); x++) {
                for (int y = 0; y < Broodwar->mapHeight(); y++) {
                    auto t = TilePosition(x, y);
                    auto p = Position(t) + Position(16, 16);

                    if (!Broodwar->isBuildable(t))
                        continue;

                    auto frameDiff = (Broodwar->getFrameCount() - Grids::lastVisibleFrame(t));
                    auto dist = p.getDistance(BWEB::Map::getMainPosition());

                    if (mapBWEM.GetArea(t) && mapBWEM.GetArea(t)->AccessibleFrom(BWEB::Map::getMainArea()))
                        groundCleanupPositions.emplace(make_pair(1.0 / (frameDiff * dist), p));
                    else
                        airCleanupPositions.emplace(make_pair(1.0 / (frameDiff * dist), p));
                }
            }
        }

        void updateUnits() {
            combatClusters.clear();
            surroundTileClaimers.clear();
            clusterTooSpread = false;
            airCluster.first = 0.0;
            airCluster.second = Positions::Invalid;
            multimap<double, UnitInfo&> combatUnitsByDistance;

            // If the checking unit hasn't died, it's still trying to check the enemy
            if (!checker.expired())
                lastCheckFrame = Broodwar->getFrameCount();

            // Determine if we need to create a new checking unit to try and detect the enemy build
            const auto needEnemyCheck = !Players::ZvZ() && !Strategy::enemyRush() && Strategy::getEnemyBuild() != "RaxFact" && !Strategy::enemyFastExpand() && Strategy::getEnemyTransition() == "Unknown" && Util::getTime() > Time(3, 45) && Util::getTime() < Time(10, 0) && Broodwar->getFrameCount() - lastCheckFrame > 240;
            if (needEnemyCheck && checker.expired()) {
                checker = Util::getClosestUnit(Units::getEnemyArmyCenter(), PlayerState::Self, [&](auto &u) {
                    return u.getRole() == Role::Combat && (u.getType() == Zerg_Zergling || u.getType() == Protoss_Zealot || u.getType() == Terran_Marine || u.getType() == Terran_Vulture);
                });
                if (checker.lock())
                    checker.lock()->setTarget(nullptr);
            }

            // If the checking unit exists and has seen something, it can be released
            else if (checker.lock()) {
                auto sawArmy = Broodwar->getFrameCount() - Grids::lastVisibleFrame(TilePosition(Units::getEnemyArmyCenter())) < 120;
                auto sawTarget = checker.lock()->hasTarget() && (checker.lock()->getTarget().unit()->exists() || checker.lock()->getTarget().getType().isBuilding());
                if (sawArmy || sawTarget)
                    checker.reset();
            }

            // Sort units by distance to destination
            for (auto &u : Units::getUnits(PlayerState::Self)) {
                auto &unit = *u;

                // Don't update if
                if (!unit.unit()->isCompleted()
                    || unit.getType() == Terran_Vulture_Spider_Mine
                    || unit.getType() == Protoss_Scarab
                    || unit.getType() == Protoss_Interceptor
                    || unit.getType().isSpell())
                    continue;

                // Check if we need to pull/push workers to/from combat role
                if (unit.getType().isWorker())
                    updateRole(unit);

                // Update combat role units states and sort by distance to destination
                if (unit.getRole() == Role::Combat) {
                    updateClusters(unit);
                    updateGlobalState(unit);
                    updateLocalState(unit);
                    auto dist = unit.hasTarget() ? unit.getPosition().getDistance(unit.getTarget().getPosition()) : 1.0;
                    combatUnitsByDistance.emplace(dist, unit);
                }
            }

            // Get an air commander
            auto airCommander = Util::getClosestUnit(airCluster.second, PlayerState::Self, [&](auto &u) {
                return u.isLightAir() && !u.localRetreat();
            });
            if (airCommander) {

                if (airCommander->hasSimTarget()) {
                    if (lastSimPositions.size() >= 10) {
                        if (TilePosition(lastSimPositions.front()) != TilePosition(airCommander->getSimTarget().getTilePosition())) {
                            lastSimPositions.pop_back();
                            lastSimPositions.insert(lastSimPositions.begin(), airCommander->getSimTarget().getPosition());
                        }
                    }
                    else
                        lastSimPositions.insert(lastSimPositions.begin(), airCommander->getSimTarget().getPosition());
                }

                // Execute the air commanders commands
                Horizon::simulate(*airCommander);
                updateDestination(*airCommander);
                updatePath(*airCommander);
                updateDecision(*airCommander);

                airClusterPath = airCommander->getDestinationPath();

                // Setup air commander commands for other units to follow
                airCommanderCommand = make_pair(airCommander->unit()->getLastCommand().getType(), airCommander->unit()->getLastCommand().getTargetPosition());
            }

            // Execute commands ordered by ascending distance
            for (auto &u : combatUnitsByDistance) {
                auto &unit = u.second;

                // Light air close to the air cluster use the same command of the air commander
                auto chaseDownFastUnit = unit.hasTarget() && unit.getSpeed() < unit.getTarget().getSpeed();
                if (unit.isLightAir() && !unit.isNearSplash() && !chaseDownFastUnit && !unit.localRetreat() && unit.getPosition().getDistance(airCluster.second) < max(64, 10 * com(unit.getType())) && !airCommander->isNearSuicide() && !unit.isNearSuicide()) {

                    if (unit.getPosition().getDistance(airCluster.second) > 32.0)
                        clusterTooSpread = true;

                    Horizon::simulate(unit);
                    updateDestination(unit);

                    auto percentMoveCloser = clamp(unit.getPosition().getDistance(airCluster.second) / 64.0, 0.0, 1.0);

                    if (unit.hasTarget() && unit.isWithinRange(unit.getTarget()) && airCommanderCommand.first == UnitCommandTypes::Attack_Unit) {
                        unit.command(UnitCommandTypes::Attack_Unit, unit.getTarget());
                        continue;
                    }
                    else if (airCommanderCommand.first == UnitCommandTypes::Move) {
                        auto positionx = (airCommanderCommand.second.x * (1.0 - percentMoveCloser)) + (airCluster.second.x * percentMoveCloser);
                        auto positiony = (airCommanderCommand.second.y * (1.0 - percentMoveCloser)) + (airCluster.second.y * percentMoveCloser);                        
                        unit.command(UnitCommandTypes::Move, Position(positionx, positiony));
                        Broodwar->drawCircleMap(airCommanderCommand.second, 4, Colors::Purple, true);
                        Broodwar->drawLineMap(unit.getPosition(), Position(positionx, positiony), Colors::Purple);
                        continue;
                    }
                }

                // Combat unit decisions
                if (unit.getRole() == Role::Combat) {
                    Horizon::simulate(unit);
                    updateDestination(unit);
                    updatePath(unit);
                    updateDecision(unit);
                }
            }
        }

        void updateRetreatPositions()
        {
            retreatPositions.clear();

            if (Terrain::getDefendChoke() == BWEB::Map::getMainChoke()) {
                retreatPositions.insert(Terrain::getMyMain()->getResourceCentroid());
                return;
            }

            for (auto &[unit, station] : Stations::getMyStations()) {
                auto posBest = Positions::Invalid;
                auto distBest = DBL_MAX;
                auto tile = station->getBase()->Location();

                if (!unit->isCompleted() || unit->isMorphing())
                    continue;

                // Find a TilePosition around it that is suitable to path to
                for (int x = tile.x - 2; x < tile.x + 6; x++) {
                    for (int y = tile.y - 2; y < tile.y + 6; y++) {
                        TilePosition t(x, y);
                        Position center = Position(t) + Position(16, 16);
                        auto dist = center.getDistance(mapBWEM.Center());
                        if (t.isValid() && dist < distBest && BWEB::Map::isUsed(t) == None) {
                            posBest = center;
                            distBest = dist;
                        }
                    }
                }

                // If valid, add to set of retreat positions
                if (posBest.isValid())
                    retreatPositions.insert(posBest);
            }
        }

        void updateDefenders()
        {
            // Update all my buildings
            for (auto &u : Units::getUnits(PlayerState::Self)) {
                auto &unit = *u;

                if (unit.getRole() == Role::Defender)
                    updateDecision(unit);
            }
        }
    }

    void onStart()
    {
        if (!BWEB::Map::getMainChoke())
            return;

        const auto createCache = [&](const BWEM::ChokePoint * chokePoint, const BWEM::Area * area) {
            auto center = chokePoint->Center();
            for (int x = center.x - 50; x <= center.x + 50; x++) {
                for (int y = center.y - 50; y <= center.y + 50; y++) {
                    WalkPosition w(x, y);
                    const auto p = Position(w) + Position(4, 4);

                    if (!p.isValid()
                        || (area && mapBWEM.GetArea(w) != area)
                        || Grids::getMobility(w) < 6)
                        continue;

                    auto closest = Util::getClosestChokepoint(p);
                    if (closest != chokePoint && p.getDistance(Position(closest->Center())) < 96.0 && (closest == BWEB::Map::getMainChoke() || closest == BWEB::Map::getNaturalChoke()))
                        continue;

                    concaveCache[chokePoint].push_back(w);
                }
            }
        };

        // Main area for defending sometimes is wrong like Andromeda and Polaris Rhapsody
        const BWEM::Area * defendArea = nullptr;
        auto &[a1, a2] = BWEB::Map::getMainChoke()->GetAreas();
        if (a1 && Terrain::isInAllyTerritory(a1))
            defendArea = a1;
        if (a2 && Terrain::isInAllyTerritory(a2))
            defendArea = a2;

        createCache(BWEB::Map::getMainChoke(), defendArea);
        createCache(BWEB::Map::getMainChoke(), BWEB::Map::getMainArea());

        // Natural area should always be correct
        createCache(BWEB::Map::getNaturalChoke(), BWEB::Map::getNaturalArea());
    }

    void onFrame() {
        Visuals::startPerfTest();
        updateCleanup();
        updateUnits();
        updateDefenders();
        updateRetreatPositions();
        Visuals::endPerfTest("Combat");
    }

    Position getClosestRetreatPosition(UnitInfo& unit)
    {
        auto distBest = DBL_MAX;
        auto posBest = Positions::Invalid;
        for (auto &position : retreatPositions) {
            auto dist = position.getDistance(unit.getPosition());
            if (dist < distBest && dist > unit.getSimRadius()) {
                posBest = position;
                distBest = dist;
            }
        }
        return posBest;
    }

    void resetDefendPositionCache() {
        defendPositions.clear();
    }

    set<Position> getDefendPositions() { return defendPositions; }
    multimap<double, Position>& getCombatClusters() { return combatClusters; }
    Position getAirClusterCenter() { return airCluster.second; }
}