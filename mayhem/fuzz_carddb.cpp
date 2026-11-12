// Fuzzes the card-database loader (DataManager::ReadDB) and the lookups/formatters the client runs over
// its result. The fuzz bytes are decoded into the rows of a datas/texts database with ygopro's schema
// (see carddb_input.h), so the card-decoding paths (link markers, pendulum scales, alias/rule codes,
// setcodes, NULL and odd text columns) are reached by ordinary mutations. A raw SQLite image would be
// rejected by SQLite's page-layout checks before ReadDB runs.
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <sqlite3.h>
#include "harness_common.h"
#include "carddb_input.h"
#include "data_manager.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) {
	mayhem::InitLocale();
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	sqlite3* db = nullptr;
	if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
		sqlite3_close(db);
		return 0;
	}
	if (!mayhem::BuildCardDB(db, data, size)) {
		sqlite3_close(db);
		return 0;
	}
	ygo::DataManager manager;
	bool ok = manager.ReadDB(db);
	sqlite3_close(db);
	if (!ok)
		return 0;

	// A few of strings.conf's lines, constant for every input, so the formatters' lookups can hit.
	manager.ReadStringConfLine("!system 1010 LIGHT\n");
	manager.ReadStringConfLine("!system 1020 Warrior\n");
	manager.ReadStringConfLine("!system 1050 Monster\n");
	manager.ReadStringConfLine("!setname 0x8f Alpha|Beta\n");
	manager.ReadStringConfLine("!setname 0x54 Gamma\n");

	volatile size_t sink = 0;
	std::vector<const ygo::CardDataC*> cards;
	for (const auto& entry : manager.GetDataTable()) {
		const ygo::CardDataC& cd = entry.second;
		cards.push_back(&cd);
		sink += manager.FormatType(cd.type).size();
		sink += manager.FormatRace(cd.race).size();
		sink += manager.FormatAttribute(cd.attribute).size();
		sink += manager.FormatSetName(cd.setcode).size();
		sink += manager.FormatLinkMarker(cd.link_marker).size();
		sink += std::wcslen(manager.GetName(entry.first));
		sink += std::wcslen(manager.GetText(entry.first));
		for (int i = 0; i < ygo::DESC_COUNT; ++i)
			sink += std::wcslen(manager.GetDesc((entry.first << 4) | i));
		sink += cd.get_duel_code();
		sink += cd.is_setcodes({0x8f, 0x54});
		ygo::CardData raw{};
		sink += manager.GetData(entry.first, &raw);
		ygo::CardString str;
		sink += manager.GetString(entry.first, &str);
		sink += ygo::DataManager::CardNameContains(manager.GetName(entry.first), L"a");
	}
	sink += manager.GetSetCodes(L"Alpha").size();
	sink += manager.GetSetCodes(L"a").size();
	sink += manager.GetSetCodes(L"ma").size();

	std::sort(cards.begin(), cards.end(), ygo::DataManager::deck_sort_lv);
	std::sort(cards.begin(), cards.end(), ygo::DataManager::deck_sort_atk);
	std::sort(cards.begin(), cards.end(), ygo::DataManager::deck_sort_def);
	std::sort(cards.begin(), cards.end(), ygo::DataManager::deck_sort_id);
	for (const auto* c : cards)
		sink += c->code;
	(void)sink;
	return 0;
}
