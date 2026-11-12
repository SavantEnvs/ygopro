// Known-answer tests for the ygopro data layer (replay, banlist, deck, card DB, strings.conf).
// Every check prints exactly one "PASS <name>" or "FAIL <name>" line; mayhem/test.sh counts them.
// With --emit-seeds <dir> it instead writes the starter fuzz seeds (see mayhem/*/testsuite).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <sqlite3.h>
#include "harness_common.h"
#include "testdb.h"
#include "carddb_input.h"
#include "data_manager.h"
#include "deck_manager.h"
#include "replay.h"
#include "network.h"

static int g_pass, g_fail;

#define CHECK(name, cond) do { \
	if (cond) { ++g_pass; std::printf("PASS %s\n", name); } \
	else { ++g_fail; std::printf("FAIL %s\n", name); } \
} while (0)

static std::string ReadAll(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void WriteAll(const std::string& path, const std::string& bytes) {
	std::ofstream out(path, std::ios::binary);
	out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

template<typename T>
static void Put(std::string& s, const T& v) {
	s.append(reinterpret_cast<const char*>(&v), sizeof v);
}

static void PutName(std::string& s, const char* name) {
	uint16_t buf[20]{};
	for (int i = 0; name[i] && i < 19; ++i)
		buf[i] = static_cast<uint16_t>(name[i]);
	Put(s, buf);
}

static const uint32_t kMain0[] = {5000, 5001, 5002, 1001};
static const uint32_t kExtra0[] = {2001};
static const uint32_t kMain1[] = {5003, 5004};
static const uint8_t kResp1[] = {0x01, 0x02, 0x03};
static const uint8_t kResp2[] = {0xaa};

// Replay payload after the header: names, duel parameters, two decks, two responses.
static std::string ReplayBody() {
	std::string b;
	PutName(b, "Alice");
	PutName(b, "Bob");
	ygo::DuelParameters params{8000, 5, 1, 0x1204};
	Put(b, params);
	Put<uint32_t>(b, 4);
	for (uint32_t c : kMain0) Put(b, c);
	Put<uint32_t>(b, 1);
	for (uint32_t c : kExtra0) Put(b, c);
	Put<uint32_t>(b, 2);
	for (uint32_t c : kMain1) Put(b, c);
	Put<uint32_t>(b, 0);
	Put<uint8_t>(b, sizeof kResp1);
	b.append(reinterpret_cast<const char*>(kResp1), sizeof kResp1);
	Put<uint8_t>(b, sizeof kResp2);
	b.append(reinterpret_cast<const char*>(kResp2), sizeof kResp2);
	return b;
}

static bool CheckReplay(ygo::Replay& r, const std::string& path, const char* tag) {
	std::wstring wpath(path.begin(), path.end());
	char label[128];
	auto name = [&](const char* what) { std::snprintf(label, sizeof label, "replay[%s] %s", tag, what); return label; };
	bool opened = r.OpenReplay(wpath.c_str());
	CHECK(name("opens"), opened);
	if (!opened)
		return false;
	CHECK(name("two players"), r.players.size() == 2);
	CHECK(name("player names"), r.players.size() == 2 && r.players[0] == L"Alice" && r.players[1] == L"Bob");
	CHECK(name("duel parameters"), r.params.start_lp == 8000 && r.params.start_hand == 5 &&
		r.params.draw_count == 1 && r.params.duel_flag == 0x1204);
	CHECK(name("deck count"), r.decks.size() == 2);
	CHECK(name("deck contents"), r.decks.size() == 2 && r.decks[0].main.size() == 4 && r.decks[0].main[3] == 1001 &&
		r.decks[0].extra.size() == 1 && r.decks[0].extra[0] == 2001 && r.decks[1].main.size() == 2 &&
		r.decks[1].main[1] == 5004 && r.decks[1].extra.empty());
	r.SkipInfo();
	unsigned char resp[256]{};
	bool first = r.ReadNextResponse(resp);
	CHECK(name("first response"), first && std::memcmp(resp, kResp1, sizeof kResp1) == 0);
	bool second = r.ReadNextResponse(resp);
	CHECK(name("second response"), second && resp[0] == 0xaa);
	CHECK(name("no third response"), !r.ReadNextResponse(resp));
	return true;
}

static std::string g_seeds;

static void Seed(const char* target, const char* name, const std::string& bytes) {
	if (!g_seeds.empty())
		WriteAll(g_seeds + "/" + target + "/testsuite/" + name, bytes);
}

static void TestReplay() {
	ygo::ExtendedReplayHeader header;
	header.base.id = REPLAY_ID_YRP2;
	header.base.version = 0x1362;
	header.base.flag = REPLAY_UNIFORM;
	header.base.seed = 0xdeadbeef;
	header.base.start_time = 1700000000;
	std::string body = ReplayBody();

	// Compressed round trip through the recorder (writes ./replay/ under the current directory).
	{
		ygo::Replay rec;
		rec.BeginRecord();
		rec.WriteHeader(header);
		const size_t info_len = 2 * 40 + 16 + (4 + 16 + 4 + 4) + (4 + 8 + 4);
		rec.WriteData(body.data(), info_len);
		rec.WriteResponse(kResp1, sizeof kResp1);
		rec.WriteResponse(kResp2, sizeof kResp2);
		rec.EndRecord();
		bool saved = rec.SaveReplay(L"roundtrip");
		CHECK("replay[compressed] saved", saved);
		ygo::Replay in;
		CheckReplay(in, "./replay/roundtrip.yrp", "compressed");
		Seed("fuzz_replay", "compressed.yrp", ReadAll("./replay/roundtrip.yrp"));
	}

	// Uncompressed YRP1 file with the same payload.
	{
		ygo::ReplayHeader base = header.base;
		base.id = REPLAY_ID_YRP1;
		std::string file;
		Put(file, base);
		file += body;
		WriteAll("./replay/plain.yrp", file);
		ygo::Replay in;
		CheckReplay(in, "./replay/plain.yrp", "plain");
		Seed("fuzz_replay", "plain.yrp", file);
	}

	// Rejections: wrong id, too-old version, non-uniform new version, truncated info.
	{
		ygo::ReplayHeader base = header.base;
		std::string good;
		Put(good, base);
		good += body;
		ygo::Replay r;
		std::string bad = good;
		bad[0] = 'X';
		WriteAll("./replay/bad_id.yrp", bad);
		CHECK("replay[reject] bad id", !r.OpenReplay(L"./replay/bad_id.yrp"));
		ygo::ReplayHeader old = base;
		old.id = REPLAY_ID_YRP1;
		old.version = 0x12cf;
		std::string oldfile;
		Put(oldfile, old);
		oldfile += body;
		WriteAll("./replay/old.yrp", oldfile);
		CHECK("replay[reject] old version", !r.OpenReplay(L"./replay/old.yrp"));
		ygo::ReplayHeader nonuni = base;
		nonuni.id = REPLAY_ID_YRP1;
		nonuni.flag = 0;
		std::string nu;
		Put(nu, nonuni);
		nu += body;
		WriteAll("./replay/nonuniform.yrp", nu);
		CHECK("replay[reject] non-uniform", !r.OpenReplay(L"./replay/nonuniform.yrp"));
		ygo::ReplayHeader h1 = base;
		h1.id = REPLAY_ID_YRP1;
		std::string trunc;
		Put(trunc, h1);
		trunc += body.substr(0, 90);
		WriteAll("./replay/trunc.yrp", trunc);
		CHECK("replay[reject] truncated info", !r.OpenReplay(L"./replay/trunc.yrp"));
	}
}

static void TestLFList(const std::string& dir) {
	const char* text =
		"#comment line\n"
		"!Test List 2026\n"
		"1001 1\n1002 2\n1003 0\n1005 3\nabc 1\n"
		"!Second\n"
		"2001 1\n1004 0\n";
	std::string path = dir + "/lflist.conf";
	WriteAll(path, text);
	ygo::DeckManager dm;
	dm.LoadLFListSingle(path.c_str());
	CHECK("lflist two lists", dm._lfList.size() == 2);
	if (dm._lfList.size() == 2) {
		const auto& a = dm._lfList[0];
		const auto& b = dm._lfList[1];
		CHECK("lflist names", a.listName == L"Test List 2026" && b.listName == L"Second");
		CHECK("lflist content", a.content.size() == 3 && a.content.at(1001) == 1 && a.content.at(1002) == 2 &&
			a.content.at(1003) == 0 && !a.content.count(1005) && b.content.size() == 2);
		// Hashes computed independently: h = 0x7dfcee6a ^ sum over (code,count) of rotl(code,18) ^ rotl(code,27+count).
		CHECK("lflist hash first", a.hash == 0xfa5cee36u);
		CHECK("lflist hash second", b.hash == 0x1d08ee08u);
		CHECK("lflist lookup by hash", dm.GetLFList(0xfa5cee36u) == &a && dm.GetLFList(1) == nullptr);
	}
	Seed("fuzz_lflist", "small.conf", text);
}

static std::string Ydk(const std::vector<uint32_t>& main, const std::vector<uint32_t>& extra,
	const std::vector<uint32_t>& side) {
	std::string s = "#created by test\n#main\n";
	for (auto c : main) s += std::to_string(c) + "\n";
	s += "#extra\n";
	for (auto c : extra) s += std::to_string(c) + "\n";
	s += "!side\n";
	for (auto c : side) s += std::to_string(c) + "\n";
	return s;
}

static ygo::DeckManager* g_lists;

static uint32_t Check(const ygo::Deck& deck, size_t list, size_t rule) {
	return g_lists->CheckDeck(deck, g_lists->_lfList[list].hash, rule);
}

static void TestDeck() {
	std::vector<uint32_t> main;
	for (uint32_t i = 0; i < 40; ++i) main.push_back(5000 + i / 2);
	std::string ydk = Ydk(main, {2001, 2002, 9999999}, {1003, 1004});
	ygo::Deck deck;
	std::istringstream in(ydk);
	uint32_t err = ygo::DeckManager::LoadDeckFromStream(deck, in, false);
	CHECK("deck sizes", deck.main.size() == 40 && deck.extra.size() == 2 && deck.side.size() == 2);
	CHECK("deck unknown card reported", err == 9999999);
	CHECK("deck card identity", deck.main[0]->code == 5000 && deck.extra[1]->code == 2002 && deck.side[0]->code == 1003);
	CHECK("deck legal without limits", Check(deck, 2, 5) == 0);
	CHECK("deck forbidden card", Check(deck, 0, 5) == ((DECKERROR_LFLIST << 28) | 5000));

	std::vector<uint32_t> small(main.begin(), main.begin() + 10);
	ygo::Deck d3;
	std::istringstream in3(Ydk(small, {}, {}));
	ygo::DeckManager::LoadDeckFromStream(d3, in3, false);
	CHECK("deck too small", Check(d3, 2, 5) == ((DECKERROR_MAINCOUNT << 28) | 10));

	std::vector<uint32_t> quad = main;
	for (int i = 0; i < 4; ++i) quad[10 + i] = 5010;
	ygo::Deck d4;
	std::istringstream in4(Ydk(quad, {}, {}));
	ygo::DeckManager::LoadDeckFromStream(d4, in4, false);
	CHECK("deck four copies", Check(d4, 2, 5) == ((DECKERROR_CARDCOUNT << 28) | 5010));

	std::stringstream out;
	ygo::DeckManager::SaveDeck(deck, out);
	std::string saved = out.str();
	CHECK("deck save format", saved.rfind("#created by ...\n#main\n5000\n5000\n5001\n", 0) == 0 &&
		saved.find("#extra\n2001\n2002\n!side\n1003\n1004\n") != std::string::npos);

	ygo::Deck pack;
	std::istringstream in5("1001\n1001\n2001\n3001\n!\n1002\n");
	uint32_t perr = ygo::DeckManager::LoadDeckFromStream(pack, in5, true);
	CHECK("deck packlist keeps extra in main", pack.main.size() == 3 && pack.side.size() == 1 && perr == 3001);

	auto seed = [&](const char* name, uint8_t rule, uint8_t list, const std::string& text) {
		Seed("fuzz_deck", name, std::string{static_cast<char>(rule), static_cast<char>(list)} + text);
	};
	seed("legal.ydk", 5, 0, ydk);
	seed("banned.ydk", 5, 0, Ydk(main, {}, {}));
	seed("small.ydk", 0, 1, Ydk(small, {2003, 2004}, {1001}));
	seed("quad.ydk", 4, 2, Ydk(quad, {}, {}));
}

static void TestCardDB() {
	sqlite3* db = nullptr;
	sqlite3_open(":memory:", &db);
	bool built = mayhem::PopulateTestDB(db);
	CHECK("carddb fixture built", built);
	ygo::DataManager dm;
	CHECK("carddb ReadDB", dm.ReadDB(db));
	CHECK("carddb row count", dm.GetDataTable().size() == 12 + 20);
	auto it = dm.GetCodePointer(2004);
	CHECK("carddb link marker moved out of def", it != dm.GetDataTable().end() && it->second.link_marker == 0x28 &&
		it->second.defense == 0 && it->second.attack == 2000);
	auto pend = dm.GetCodePointer(1007);
	CHECK("carddb pendulum scales", pend != dm.GetDataTable().end() && pend->second.lscale == 1 &&
		pend->second.rscale == 8 && pend->second.level == 4);
	auto far = dm.GetCodePointer(1006);
	CHECK("carddb rule alias", far != dm.GetDataTable().end() && far->second.rule_code == 777 && far->second.alias == 0);
	auto near = dm.GetCodePointer(1005);
	CHECK("carddb artwork alias kept", near != dm.GetDataTable().end() && near->second.alias == 1001 &&
		near->second.rule_code == 0 && near->second.get_duel_code() == 1001);
	CHECK("carddb names", std::wstring(dm.GetName(1001)) == L"Alpha Warrior" && std::wstring(dm.GetText(1003)) == L"Draw 2 cards." &&
		std::wstring(dm.GetName(424242)) == L"???");
	CHECK("carddb setcode", dm.GetCodePointer(1001)->second.setcode[0] == 0x8f);
	ygo::CardData cd{};
	CHECK("carddb GetData", dm.GetData(1001, &cd) && cd.code == 1001 && cd.attack == 1800 && cd.defense == 1000);

	CHECK("normalize accents", ygo::DataManager::NormalizeChar(L'é') == L'E' && ygo::DataManager::NormalizeChar(L'z') == L'Z');
	CHECK("name search", ygo::DataManager::CardNameContains(L"Alpha Warrior", L"alpha") &&
		ygo::DataManager::CardNameContains(L"Café Cat", L"CAFE") && !ygo::DataManager::CardNameContains(L"Alpha", L"warrior"));

	// fuzz_carddb's input format: the same cards, encoded, must decode to the same database rows.
	std::string encoded;
	for (const auto& c : mayhem::kTestCards)
		encoded += mayhem::EncodeCard(c, c.id == 1005);
	sqlite3* db2 = nullptr;
	sqlite3_open(":memory:", &db2);
	ygo::DataManager dm2;
	bool decoded = mayhem::BuildCardDB(db2, reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size()) && dm2.ReadDB(db2);
	CHECK("carddb input decodes to the fixture rows", decoded && dm2.GetDataTable().size() == 12 &&
		dm2.GetCodePointer(2004)->second.link_marker == 0x28 && dm2.GetCodePointer(1007)->second.rscale == 8 &&
		dm2.GetCodePointer(1006)->second.rule_code == 777 && std::wstring(dm2.GetName(1001)) == L"Alpha Warrior");
	CHECK("carddb input relative alias", decoded && dm2.GetCodePointer(1005)->second.alias == 1001 &&
		dm2.GetCodePointer(1005)->second.rule_code == 0 && std::wstring(dm2.GetText(1005)) == L"Alternate artwork.");
	sqlite3_close(db2);

	if (!g_seeds.empty()) {
		Seed("fuzz_carddb", "all_cards.bin", encoded);
		for (const auto& c : mayhem::kTestCards)
			if (c.id == 1005 || c.id == 1006 || c.id == 1007 || c.id == 2004)
				Seed("fuzz_carddb", (std::string("card_") + std::to_string(c.id) + ".bin").c_str(), mayhem::EncodeCard(c, c.id == 1005));
	}
	sqlite3_close(db);
}

static void TestStrings(const std::string& dir) {
	const char* text =
		"#comment\n"
		"!system 1 Normal Summon\n"
		"!system 1050 Monster\n"
		"!victory 0x10 Exodia\n"
		"!counter 0x1 Spell Counter\n"
		"!setname 0x8f Test Set\tcomment tail\n"
		"!setname 0x54 Alpha|Beta\n"
		"!bogus 1 ignored\n";
	std::string path = dir + "/strings.conf";
	WriteAll(path, text);
	ygo::DataManager dm;
	CHECK("strings load", dm.LoadStrings(path.c_str()));
	CHECK("strings system", std::wstring(dm.GetSysString(1)) == L"Normal Summon" && std::wstring(dm.GetSysString(2)) == L"???");
	CHECK("strings victory", std::wstring(dm.GetVictoryString(0x10)) == L"Exodia");
	CHECK("strings counter", std::wstring(dm.GetCounterName(1)) == L"Spell Counter");
	CHECK("strings setname tab comment", std::wstring(dm.GetSetName(0x8f)) == L"Test Set");
	CHECK("strings setname lookup", dm.GetSetCodes(L"Beta") == std::vector<uint32_t>{0x54} && dm.GetSetCodes(L"Nope").empty());
	CHECK("strings format type", dm.FormatType(0x2).find(L"???") == 0 && dm.FormatType(0).size() == 3);
	CHECK("strings desc", std::wstring(dm.GetDesc(1)) == L"Normal Summon");
	Seed("fuzz_strings", "small.conf", text);
}

int main(int argc, char** argv) {
	mayhem::InitLocale();
	if (argc == 3 && std::strcmp(argv[1], "--emit-seeds") == 0)
		g_seeds = argv[2];
	char tmpl[] = "/tmp/ygopro-selftest-XXXXXX";
	char* dir = mkdtemp(tmpl);
	if (!dir || chdir(dir) != 0) {
		std::printf("FAIL scratch directory\n");
		return 1;
	}
	mkdir("./replay", 0755);

	sqlite3* db = nullptr;
	sqlite3_open(":memory:", &db);
	mayhem::PopulateTestDB(db);
	ygo::dataManager.ReadDB(db);
	sqlite3_close(db);
	WriteAll(std::string(dir) + "/lists.conf",
		"!Limited 1\n1001 1\n1002 2\n1003 0\n5000 0\n5001 1\n2001 1\n!Limited 2\n1004 0\n5002 1\n5010 3\n!Open\n");
	g_lists = new ygo::DeckManager();
	g_lists->LoadLFListSingle((std::string(dir) + "/lists.conf").c_str());

	TestReplay();
	TestLFList(dir);
	TestDeck();
	TestCardDB();
	TestStrings(dir);
	std::printf("SELFTEST: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
