// Shared by the harness and the selftest: a small synthetic card database with the same schema as
// ygopro's cards.cdb (datas + texts), so deck/DB code paths have real card data to work on.
#ifndef MAYHEM_TESTDB_H
#define MAYHEM_TESTDB_H

#include <cstdio>
#include <string>
#include <sqlite3.h>

namespace mayhem {

struct TestCard {
	unsigned id, ot, alias;
	long long setcode;
	unsigned type;
	int atk, def;
	unsigned level;
	unsigned race, attribute;
	const char* name;
	const char* desc;
};

// Cards 5000..5019 are 20 filler normal monsters, so 40-card main decks can be built within the 3-copy limit.
static const TestCard kTestCards[] = {
	{1001, 3, 0, 0x8f, 0x11, 1800, 1000, 4, 0x1, 0x10, "Alpha Warrior", "A plain warrior."},
	{1002, 3, 0, 0x54, 0x21, 1500, 1200, 4, 0x2, 0x20, "Beta Mage", "Effect monster."},
	{1003, 3, 0, 0, 0x2, 0, 0, 0, 0, 0, "Gamma Spell", "Draw 2 cards."},
	{1004, 1, 0, 0, 0x4, 0, 0, 0, 0, 0, "Delta Trap", "Negate an attack."},
	{1005, 3, 1001, 0x8f, 0x11, 1800, 1000, 4, 0x1, 0x10, "Alpha Warrior (alt art)", "Alternate artwork."},
	{1006, 2, 777, 0, 0x21, 1000, 1000, 3, 0x1, 0x10, "Rule Alias", "Alias far from its original."},
	{1007, 3, 0, 0, 0x1000021, 2400, 800, 0x01080004, 0x8, 0x20, "Pendulum Dragon", "Scales 1 and 8."},
	{2001, 3, 0, 0, 0x41, 2500, 2000, 6, 0x8, 0x20, "Fusion Dragon", "Fusion monster."},
	{2002, 3, 0, 0, 0x2001, 2300, 1800, 7, 0x1, 0x10, "Synchro Knight", "Synchro monster."},
	{2003, 3, 0, 0, 0x800001, 2000, 1500, 4, 0x2, 0x20, "Xyz Wizard", "Xyz monster."},
	{2004, 3, 0, 0, 0x4000001, 2000, 0x28, 2, 0x20, 0x10, "Link Guardian", "Link monster, markers 0x28."},
	{3001, 3, 0, 0, 0x4001, 0, 0, 1, 0x1, 0x10, "Test Token", "Tokens are never deck cards."},
};

inline bool CreateSchema(sqlite3* db) {
	const char* schema =
		"CREATE TABLE datas(id integer primary key, ot integer, alias integer, setcode integer, type integer,"
		" atk integer, def integer, level integer, race integer, attribute integer, category integer);"
		"CREATE TABLE texts(id integer primary key, name text, desc text, str1 text, str2 text, str3 text,"
		" str4 text, str5 text, str6 text, str7 text, str8 text, str9 text, str10 text, str11 text, str12 text,"
		" str13 text, str14 text, str15 text, str16 text);";
	return sqlite3_exec(db, schema, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool PopulateTestDB(sqlite3* db) {
	if (!CreateSchema(db))
		return false;
	auto insert = [&](const TestCard& c) {
		char sql[1024];
		std::snprintf(sql, sizeof sql,
			"INSERT INTO datas VALUES(%u,%u,%u,%lld,%u,%d,%d,%u,%u,%u,0);"
			"INSERT INTO texts(id,name,desc,str1) VALUES(%u,'%s','%s','');",
			c.id, c.ot, c.alias, c.setcode, c.type, c.atk, c.def, c.level, c.race, c.attribute, c.id, c.name, c.desc);
		return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
	};
	for (const auto& c : kTestCards)
		if (!insert(c))
			return false;
	for (unsigned i = 0; i < 20; ++i) {
		std::string name = "Filler " + std::to_string(i);
		TestCard c{5000 + i, 3, 0, 0, 0x11, 1000, 1000, 4, 0x1, 0x10, name.c_str(), "Filler."};
		if (!insert(c))
			return false;
	}
	return true;
}

}

#endif
