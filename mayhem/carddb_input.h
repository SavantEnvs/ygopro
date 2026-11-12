// Fuzz input format for fuzz_carddb: a card database built from the bytes, instead of a raw SQLite image
// (which SQLite rejects before DataManager::ReadDB runs). The decoder builds the datas/texts tables with
// ygopro's schema; the encoder is used by the selftest to write the seed corpus and to check the decoder.
//
// Input = zero or more records, each: 48 fixed bytes (little-endian)
//   id u32, ot u32, alias u32, setcode u64, type u32, atk i32, def i32, level u32, race u32, attribute u32,
//   category u32
// then a u32 mask (bit 0 = name, bit 1 = desc, bit 2+i = str<i+1>), then for each set bit a u8 length and
// that many bytes of text; a clear bit leaves the column NULL. If alias has bit 31 set it is read as a
// 16-bit signed distance from id, so an artwork alias next to its card is reachable. Trailing bytes too
// short for a record are ignored.
#ifndef MAYHEM_CARDDB_INPUT_H
#define MAYHEM_CARDDB_INPUT_H

#include <cstdint>
#include <cstring>
#include <string>
#include <sqlite3.h>
#include "testdb.h"

namespace mayhem {

constexpr size_t kCardRecordBytes = 48;
constexpr size_t kMaxCardRecords = 64;
constexpr int kTextColumns = 18;

inline void PutLE(std::string& out, uint64_t v, int bytes) {
	for (int i = 0; i < bytes; ++i)
		out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

inline std::string EncodeCard(const TestCard& c, bool alias_relative = false) {
	std::string out;
	PutLE(out, c.id, 4);
	PutLE(out, c.ot, 4);
	PutLE(out, alias_relative ? (0x80000000u | (static_cast<uint16_t>(c.alias - c.id))) : c.alias, 4);
	PutLE(out, static_cast<uint64_t>(c.setcode), 8);
	PutLE(out, c.type, 4);
	PutLE(out, static_cast<uint32_t>(c.atk), 4);
	PutLE(out, static_cast<uint32_t>(c.def), 4);
	PutLE(out, c.level, 4);
	PutLE(out, c.race, 4);
	PutLE(out, c.attribute, 4);
	PutLE(out, 0, 4);
	PutLE(out, 0x3, 4);
	std::string name = c.name, desc = c.desc;
	out.push_back(static_cast<char>(name.size()));
	out += name;
	out.push_back(static_cast<char>(desc.size()));
	out += desc;
	return out;
}

class ByteReader {
public:
	ByteReader(const uint8_t* data, size_t size) : p_(data), left_(size) {}
	size_t left() const { return left_; }
	uint64_t Le(int bytes) {
		uint64_t v = 0;
		for (int i = 0; i < bytes; ++i)
			v |= static_cast<uint64_t>(p_[i]) << (8 * i);
		p_ += bytes;
		left_ -= bytes;
		return v;
	}
	const uint8_t* Bytes(size_t n) {
		const uint8_t* r = p_;
		p_ += n;
		left_ -= n;
		return r;
	}

private:
	const uint8_t* p_;
	size_t left_;
};

// Fills an empty in-memory database from `data`. Returns false only if SQLite itself fails.
inline bool BuildCardDB(sqlite3* db, const uint8_t* data, size_t size) {
	if (!CreateSchema(db))
		return false;
	sqlite3_stmt* ins_data = nullptr;
	sqlite3_stmt* ins_text = nullptr;
	if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO datas VALUES(?,?,?,?,?,?,?,?,?,?,?)", -1, &ins_data, nullptr) != SQLITE_OK)
		return false;
	std::string text_sql = "INSERT OR REPLACE INTO texts VALUES(?";
	for (int i = 0; i < kTextColumns; ++i)
		text_sql += ",?";
	text_sql += ")";
	if (sqlite3_prepare_v2(db, text_sql.c_str(), -1, &ins_text, nullptr) != SQLITE_OK) {
		sqlite3_finalize(ins_data);
		return false;
	}
	ByteReader in(data, size);
	for (size_t row = 0; row < kMaxCardRecords && in.left() >= kCardRecordBytes + 4; ++row) {
		uint32_t id = static_cast<uint32_t>(in.Le(4));
		uint32_t ot = static_cast<uint32_t>(in.Le(4));
		uint32_t alias = static_cast<uint32_t>(in.Le(4));
		if (alias & 0x80000000u)
			alias = id + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(alias & 0xffff)));
		int64_t setcode = static_cast<int64_t>(in.Le(8));
		sqlite3_bind_int64(ins_data, 1, id);
		sqlite3_bind_int64(ins_data, 2, ot);
		sqlite3_bind_int64(ins_data, 3, alias);
		sqlite3_bind_int64(ins_data, 4, setcode);
		sqlite3_bind_int64(ins_data, 5, static_cast<uint32_t>(in.Le(4)));
		sqlite3_bind_int64(ins_data, 6, static_cast<int32_t>(in.Le(4)));
		sqlite3_bind_int64(ins_data, 7, static_cast<int32_t>(in.Le(4)));
		for (int col = 8; col <= 11; ++col)
			sqlite3_bind_int64(ins_data, col, static_cast<uint32_t>(in.Le(4)));
		uint32_t mask = static_cast<uint32_t>(in.Le(4));
		sqlite3_bind_int64(ins_text, 1, id);
		for (int i = 0; i < kTextColumns; ++i) {
			if (!(mask & (1u << i)) || in.left() < 1) {
				sqlite3_bind_null(ins_text, 2 + i);
				continue;
			}
			size_t len = in.Le(1);
			if (len > in.left())
				len = in.left();
			sqlite3_bind_text(ins_text, 2 + i, reinterpret_cast<const char*>(in.Bytes(len)), static_cast<int>(len), SQLITE_TRANSIENT);
		}
		bool ok = sqlite3_step(ins_data) == SQLITE_DONE && sqlite3_step(ins_text) == SQLITE_DONE;
		sqlite3_reset(ins_data);
		sqlite3_reset(ins_text);
		if (!ok) {
			sqlite3_finalize(ins_data);
			sqlite3_finalize(ins_text);
			return false;
		}
	}
	sqlite3_finalize(ins_data);
	sqlite3_finalize(ins_text);
	return true;
}

}

#endif
