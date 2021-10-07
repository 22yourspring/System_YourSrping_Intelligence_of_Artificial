// SYSIA is made by Christian McCrave
// Twitch nicknamed it SYSIA \o/
// For any questions, email christianmccrave@gmail.com
// Bot started 01/03/2017

#include "Header.h"
#include "SYSIA.h"
#include "EventManager.h"

// Scout getting stuck

// Protoss:
// Obs to scout bases
// Interceptors targets might be convincing goons to dive tanks
// Obs/Arb suicide
// PylonManager into an onFrame event that looks at when pylon will complete

// Zerg:
// ZvZ move ovies along paths to see if more lings are coming
// Mutas stalling
// ZvZ spore logic
// Holding 2 concave positions (nat and main)
// ZvT 7 pool

// BWEB:
// Update wall code

// Vs CannonRush:
// Scout drone exploring
// 1 Combat drone pulled too early
// Approaching pylons/cannons

using namespace BWAPI;
using namespace std;
using namespace SYSIA;

void SYSIAModule::onStart()
{
    Players::onStart();
    Terrain::onStart();
    Walls::onStart();
    Buildings::onStart();
    Stations::onStart();
    Grids::onStart();
    Learning::onStart();
    Util::onStart();
    Combat::onStart();

    Broodwar->enableFlag(Flag::UserInput);
    Broodwar->setCommandOptimizationLevel(0);
    Broodwar->setLatCom(true);
    Broodwar->sendText("glhf");
    Broodwar->setLocalSpeed(Broodwar->getGameType() != BWAPI::GameTypes::Use_Map_Settings ? 0 : 42);
}

void SYSIAModule::onEnd(bool isWinner)
{
    Learning::onEnd(isWinner);
    Broodwar->sendText("ggwp");
}

void SYSIAModule::onFrame()
{
    if (Broodwar->getGameType() != GameTypes::Use_Map_Settings && Broodwar->isPaused())
        return;

    // Update game state
    Util::onFrame();

    // Update ingame information
    Players::onFrame();
    Units::onFrame();
    Grids::onFrame();

    // Update relevant map information and strategy    
    Terrain::onFrame();
    Resources::onFrame();
    Strategy::onFrame();
    BuildOrder::onFrame();
    Stations::onFrame();

    // Update gameplay of the bot
    Actions::onFrame();
    Goals::onFrame();
    Support::onFrame();
    Scouts::onFrame();
    Combat::onFrame();
    Workers::onFrame();
    Transports::onFrame();
    Buildings::onFrame();
    Production::onFrame();

    // Display information from this frame
    Visuals::onFrame();
}

void SYSIAModule::onSendText(string text)
{
    Visuals::onSendText(text);
}

void SYSIAModule::onReceiveText(Player player, string text)
{
}

void SYSIAModule::onPlayerLeft(Player player)
{
}

void SYSIAModule::onNukeDetect(Position target)
{
    Actions::addAction(nullptr, target, TechTypes::Nuclear_Strike, PlayerState::Neutral);
}

void SYSIAModule::onUnitDiscover(Unit unit)
{
    Events::onUnitDiscover(unit);
}

void SYSIAModule::onUnitEvade(Unit unit)
{
}

void SYSIAModule::onUnitShow(Unit unit)
{
}

void SYSIAModule::onUnitHide(Unit unit)
{
}

void SYSIAModule::onUnitCreate(Unit unit)
{
    Events::onUnitCreate(unit);
}

void SYSIAModule::onUnitDestroy(Unit unit)
{
    Events::onUnitDestroy(unit);
}

void SYSIAModule::onUnitMorph(Unit unit)
{
    Events::onUnitMorph(unit);
}

void SYSIAModule::onUnitRenegade(Unit unit)
{
    Events::onUnitRenegade(unit);
}

void SYSIAModule::onSaveGame(string gameName)
{
}

void SYSIAModule::onUnitComplete(Unit unit)
{
    Events::onUnitComplete(unit);
}
