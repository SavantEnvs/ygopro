// Fuzzes the strings.conf parser (!system/!victory/!counter/!setname lines) through
// DataManager::LoadStrings, plus the string lookup and card-name search helpers. Each input gets a
// fresh DataManager.
#include <string>
#include "harness_common.h"
#include "data_manager.h"

static mayhem::ScratchFile* g_scratch;

extern "C" int LLVMFuzzerInitialize(int*, char***) {
	mayhem::InitLocale();
	g_scratch = new mayhem::ScratchFile();
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (!g_scratch->Write(data, size))
		return 0;
	ygo::DataManager manager;
	if (!manager.LoadStrings(g_scratch->path().c_str()))
		return 0;

	volatile size_t sink = 0;
	for (const auto& e : manager._sysStrings) {
		sink += std::wcslen(manager.GetSysString(e.first));
		sink += std::wcslen(manager.GetDesc(e.first));
	}
	for (const auto& e : manager._victoryStrings)
		sink += std::wcslen(manager.GetVictoryString(e.first));
	for (const auto& e : manager._counterStrings)
		sink += std::wcslen(manager.GetCounterName(e.first));
	for (const auto& e : manager._setnameStrings) {
		sink += std::wcslen(manager.GetSetName(e.first));
		sink += manager.GetSetCodes(e.second).size();
		sink += manager.GetSetCodes(e.second.substr(0, 1)).size();
		wchar_t norm[64];
		ygo::DataManager::NormalizeString(e.second.c_str(), norm, 64);
		sink += ygo::DataManager::CardNameContains(e.second.c_str(), norm);
		sink += ygo::DataManager::CardNameContains(norm, e.second.c_str());
	}
	sink += manager.FormatLocation(1, 0)[0];
	sink += manager.FormatLocation(0x2, 6)[0];
	sink += manager.FormatType(0x4000001).size();
	sink += manager.FormatRace(0x3).size();
	sink += manager.FormatAttribute(0x7f).size();
	(void)sink;
	return 0;
}
