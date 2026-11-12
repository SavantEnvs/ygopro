// Fuzzes the .ydk deck parser, the deck legality check against a banlist, and deck re-serialisation.
// Input: byte 0 picks the duel rule, byte 1 picks the banlist, the rest is the .ydk text. The card
// database and banlists are fixed, read-only fixtures built once at startup.
#include <sstream>
#include <string>
#include <vector>
#include <sqlite3.h>
#include "harness_common.h"
#include "testdb.h"
#include "data_manager.h"
#include "deck_manager.h"

static ygo::DeckManager* g_lists;
static mayhem::ScratchFile* g_scratch;

static const char kLFList[] =
	"#comment\n"
	"!Limited 1\n"
	"1001 1\n1002 2\n1003 0\n5000 0\n5001 1\n2001 1\n"
	"!Limited 2\n"
	"1004 0\n5002 1\n5003 2\n1005 1\n2004 0\n"
	"!Open\n";

extern "C" int LLVMFuzzerInitialize(int*, char***) {
	mayhem::InitLocale();
	sqlite3* db = nullptr;
	if (sqlite3_open(":memory:", &db) != SQLITE_OK || !mayhem::PopulateTestDB(db) || !ygo::dataManager.ReadDB(db))
		return 1;
	sqlite3_close(db);
	g_scratch = new mayhem::ScratchFile();
	g_scratch->Write(reinterpret_cast<const uint8_t*>(kLFList), sizeof kLFList - 1);
	g_lists = new ygo::DeckManager();
	g_lists->LoadLFListSingle(g_scratch->path().c_str());
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size < 2)
		return -1;
	size_t rule = data[0] % 7;
	const ygo::LFList& list = g_lists->_lfList[data[1] % g_lists->_lfList.size()];
	std::istringstream stream(std::string(reinterpret_cast<const char*>(data + 2), size - 2));

	ygo::Deck deck;
	ygo::DeckManager::LoadDeckFromStream(deck, stream, false);
	volatile uint32_t sink = g_lists->CheckDeck(deck, list.hash, rule);

	std::stringstream out;
	ygo::DeckManager::SaveDeck(deck, out);
	std::istringstream reread(out.str());
	ygo::Deck again;
	ygo::DeckManager::LoadDeckFromStream(again, reread, false);
	if (again.main.size() != deck.main.size() || again.extra.size() != deck.extra.size() ||
	    again.side.size() != deck.side.size())
		__builtin_trap();

	ygo::Deck packlist;
	std::istringstream stream2(std::string(reinterpret_cast<const char*>(data + 2), size - 2));
	ygo::DeckManager::LoadDeckFromStream(packlist, stream2, true);
	sink += packlist.size();
	(void)sink;
	return 0;
}
