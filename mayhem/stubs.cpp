// Definitions for the three client-GUI symbols that the data-layer units (replay, deck_manager,
// data_manager) reference but never call on the paths we fuzz. They keep the link closed without
// pulling in the Irrlicht-based game window.
#include "game.h"
#include "deck_con.h"

namespace ygo {

Game* mainGame = nullptr;

void DeckBuilder::RefreshPackListScroll() {}

bool DeckBuilder::OnEvent(const irr::SEvent&) { return false; }

}
