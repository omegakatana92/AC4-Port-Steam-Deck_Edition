// PARAM.SFO: the disc's metadata, and the save file's. Byte-exact build/parse round-trip.
#include "harness.h"

#include <string>
#include <vector>

#include "common/sfo.h"

using namespace ac4;

TEST(sfo_build_parse_roundtrip) {
  sfo::Map m;
  sfo::set_string(m, "TITLE_ID", "BLUS30027", 12);
  sfo::set_string(m, "TITLE", "Armored Core 4", 128);
  sfo::set_string(m, "VERSION", "01.01", 8);
  sfo::set_int(m, "PARENTAL_LEVEL", 5);
  std::vector<u8> bytes = sfo::build(m);
  CHECK(bytes.size() >= 20);
  CHECK_EQ(bytes[0], u8(0));
  CHECK_EQ(bytes[1], u8('P'));
  CHECK_EQ(bytes[2], u8('S'));
  CHECK_EQ(bytes[3], u8('F'));
  sfo::Map r = sfo::parse(bytes);
  CHECK_EQ(sfo::get_string(r, "TITLE_ID"), std::string("BLUS30027"));
  CHECK_EQ(sfo::get_string(r, "TITLE"), std::string("Armored Core 4"));
  CHECK_EQ(sfo::get_string(r, "VERSION"), std::string("01.01"));
  CHECK_EQ(sfo::get_int(r, "PARENTAL_LEVEL"), 5u);
  CHECK_EQ(sfo::get_int(r, "MISSING", 7), 7u);
  CHECK_EQ(sfo::get_string(r, "MISSING", "def"), std::string("def"));
  CHECK_EQ(sfo::build(r), bytes);  // deterministic
}

TEST(sfo_string_reserved_size) {
  sfo::Map m;
  sfo::set_string(m, "A", "abc", 16);  // reserved 16 bytes, NUL-terminated, 4-byte aligned
  sfo::Map r = sfo::parse(sfo::build(m));
  CHECK_EQ(sfo::get_string(r, "A"), std::string("abc"));
  CHECK(r.at("A").max_len >= 16u);
}

TEST(sfo_rejects_malformed) {
  CHECK_THROWS(sfo::parse(std::vector<u8>(4, 0)));
  std::vector<u8> magic = {0, 'P', 'S', 'F'};
  magic.resize(20, 0);
  magic[16] = 5;  // five entries claimed, index not present
  CHECK_THROWS(sfo::parse(magic));
}
