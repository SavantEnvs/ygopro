// Fuzzes the .yrp replay loader: header validation, LZMA decompression and the replay info block
// (player names, duel parameters, decks or single-mode script name), then walks the recorded responses.
#include <string>
#include "harness_common.h"
#include "replay.h"

static mayhem::ScratchFile* g_scratch;

extern "C" int LLVMFuzzerInitialize(int*, char***) {
	mayhem::InitLocale();
	g_scratch = new mayhem::ScratchFile();
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (!g_scratch->Write(data, size))
		return 0;
	const std::string& p = g_scratch->path();
	std::wstring wpath(p.begin(), p.end());

	ygo::Replay replay;
	if (!replay.OpenReplay(wpath.c_str()))
		return -1;

	ygo::ExtendedReplayHeader header;
	replay.ReadHeader(header);
	volatile size_t sink = replay.players.size() + replay.decks.size() + replay.script_name.size();
	sink += replay.params.start_lp;

	unsigned char resp[256];
	replay.SkipInfo();
	while (replay.ReadNextResponse(resp))
		sink += resp[0];
	replay.Rewind();
	wchar_t name[20];
	replay.ReadName(name);
	replay.ReadInt32();
	(void)sink;
	return 0;
}
