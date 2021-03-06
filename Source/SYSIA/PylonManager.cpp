#include "SYSIA.h"

using namespace BWAPI;
using namespace std;

namespace SYSIA::Pylons
{
    namespace {
        map<TilePosition, int> smallMediumLocations, largeLocations;

        void updatePower(TilePosition start, int count)
        {
            for (int x = 0; x <= 15; x++) {
                for (int y = 0; y <= 9; y++) {
                    const auto tile = TilePosition(x, y) + start - TilePosition(8, 5);

                    if (!tile.isValid())
                        continue;

                    if (y == 0) {
                        if (x >= 4 && x <= 9)
                            largeLocations[tile] += count;
                    }
                    else if (y == 1 || y == 8) {
                        if (x >= 2 && x <= 13)
                            smallMediumLocations[tile] += count;
                        if (x >= 1 && x <= 12)
                            largeLocations[tile] += count;
                    }
                    else if (y == 2 || y == 7) {
                        if (x >= 1 && x <= 14)
                            smallMediumLocations[tile] += count;
                        if (x <= 13)
                            largeLocations[tile] += count;
                    }
                    else if (y == 3 || y == 4 || y == 5 || y == 6) {
                        if (x >= 1)
                            smallMediumLocations[tile] += count;
                        if (x <= 14)
                            largeLocations[tile] += count;
                    }
                    else if (y == 9) {
                        if (x >= 5 && x <= 10)
                            smallMediumLocations[tile] += count;
                        if (x >= 4 && x <= 9)
                            largeLocations[tile] += count;
                    }
                }
            }
        }
    }

    void storePylon(Unit unit)
    {
        updatePower(unit->getTilePosition(), 1);
    }

    void removePylon(Unit unit)
    {
        updatePower(unit->getTilePosition(), -1);
    }

    bool hasPower(TilePosition here, UnitType building)
    {
        if (building.tileHeight() == 2 && smallMediumLocations[here] != 0)
            return true;
        else if (building.tileHeight() == 3 && largeLocations[here] != 0)
            return true;
        return false;
    }
}