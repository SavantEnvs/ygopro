// Fuzzes the banlist (lflist.conf) parser and the hash/lookup helpers built on it.
#include <string>
#include "harness_common.h"
#include "data_manager.h"
#include "deck_manager.h"

static mayhem::ScratchFile* g_scratch;

extern "C" int LLVMFuzzerInitialize(int*, char***) {
	mayhem::InitLocale();
	g_scratch = new mayhem::ScratchFile();
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (!g_scratch->Write(data, size))
		return 0;
	ygo::DeckManager manager;
	manager.LoadLFListSingle(g_scratch->path().c_str());
	volatile size_t sink = 0;
	for (const auto& list : manager._lfList) {
		sink += list.content.size();
		const ygo::LFList* found = manager.GetLFList(list.hash);
		sink += found ? found->content.size() : 0;
		sink += std::wcslen(manager.GetLFListName(list.hash));
	}
	(void)sink;
	return 0;
}
