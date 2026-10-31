// Byte helpers and the string formatter shared by every tool and the runtime.
#include "harness.h"

#include <cstring>
#include <string>
#include <vector>

#include "common/bytes.h"

using namespace ac4;

TEST(bytes_big_endian_roundtrip) {
  u8 b[8] = {};
  wr_be32(b, 0x12345678u);
  CHECK_EQ(b[0], 0x12);
  CHECK_EQ(b[3], 0x78);
  CHECK_EQ(rd_be32(b), 0x12345678u);
  wr_be64(b, 0x0123456789abcdefull);
  CHECK_EQ(rd_be64(b), 0x0123456789abcdefull);
  wr_be16(b, 0xabcd);
  CHECK_EQ(rd_be16(b), u16(0xabcd));
}

TEST(bytes_little_endian_reads_match_the_host) {
  // The generated runtime reads little-endian fields (PARAM.SFO, savedata) through these.
  u8 b[4] = {0x04, 0x03, 0x02, 0x01};
  const u32 via_memcpy = [] {
    u32 v;
    std::memcpy(&v, "\x04\x03\x02\x01", 4);
    return v;
  }();
  CHECK_EQ(rd_le32(b), via_memcpy);
  CHECK_EQ(rd_le16(b), u16(0x0304));
}

TEST(bytes_hex_roundtrip) {
  const std::vector<u8> v = {0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff};
  const std::string h = hex(v.data(), v.size());
  CHECK_EQ(h, std::string("00017f80feff"));
  CHECK_EQ(unhex(h), v);
  CHECK_EQ(unhex("00 01 7F 80 FE FF"), v);
  CHECK_THROWS(unhex("abc"));
  CHECK_THROWS(unhex("zz"));
}

TEST(bytes_strprintf) {
  CHECK_EQ(strprintf("%s %d 0x%08x", "x", -3, 0xdeadbeefu), std::string("x -3 0xdeadbeef"));
  CHECK_EQ(strprintf("%llu", 18446744073709551615ull), std::string("18446744073709551615"));
  CHECK_EQ(strprintf("%s", ""), std::string(""));
  // Long output (the string grows; exercises the two-pass path).
  const std::string big = strprintf("%0*d", 4096, 7);
  CHECK_EQ(big.size(), size_t(4096));
  CHECK_EQ(big[0], '0');
  CHECK_EQ(big.back(), '7');
}
