// SHA-1 / SHA-256 against the NIST test vectors, plus the file helper the tools use to verify the
// player's files. These vectors are the reason the portable implementation can replace Windows CNG
// without changing behaviour.
#include "harness.h"

#include <cstdio>
#include <string>
#include <vector>

#include "common/crypto.h"
#include "common/sha.h"

namespace {

std::string sha1_hex(const std::string& s) {
  auto d = ac4::sha::sha1(s.data(), s.size());
  return ac4::hex(d.data(), d.size());
}
std::string sha256_hex(const std::string& s) {
  auto d = ac4::sha::sha256(s.data(), s.size());
  return ac4::hex(d.data(), d.size());
}

const char* kMsg448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
const char* kMsg896 =
    "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

std::string repeated(char c, size_t n) { return std::string(n, c); }

}  // namespace

TEST(sha1_nist_vectors) {
  CHECK_EQ(sha1_hex(""), std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
  CHECK_EQ(sha1_hex("abc"), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
  CHECK_EQ(sha1_hex(kMsg448), std::string("84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
  CHECK_EQ(sha1_hex(kMsg896), std::string("a49b2446a02c645bf419f995b67091253a04a259"));
  CHECK_EQ(sha1_hex(repeated('a', 1000000)), std::string("34aa973cd4c4daa4f61eeb2bdbad27316534016f"));
}

TEST(sha256_nist_vectors) {
  CHECK_EQ(sha256_hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  CHECK_EQ(sha256_hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  CHECK_EQ(sha256_hex(kMsg448), std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
  CHECK_EQ(sha256_hex(kMsg896), std::string("cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
  CHECK_EQ(sha256_hex(repeated('a', 1000000)), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

TEST(sha_streaming_matches_one_shot) {
  // Every chunk size around the 64-byte block boundary, including sizes larger than the message.
  const std::string msg = kMsg896;
  std::vector<size_t> chunks = {1, 2, 3, 7, 31, 32, 55, 56, 57, 63, 64, 65, 127, 128, 129, 1000};
  for (size_t step : chunks) {
    ac4::sha::Sha1Stream s1;
    ac4::sha::Sha256Stream s256;
    for (size_t i = 0; i < msg.size(); i += step) {
      const size_t n = std::min(step, msg.size() - i);
      s1.update(msg.data() + i, n);
      s256.update(msg.data() + i, n);
    }
    auto d1 = s1.finish();
    auto d256 = s256.finish();
    CHECK_EQ(ac4::hex(d1.data(), d1.size()), sha1_hex(msg));
    CHECK_EQ(ac4::hex(d256.data(), d256.size()), sha256_hex(msg));
  }
}

TEST(sha_empty_stream) {
  ac4::sha::Sha256Stream s;
  auto d = s.finish();
  CHECK_EQ(ac4::hex(d.data(), d.size()), sha256_hex(""));
  ac4::sha::Sha1Stream s1;
  auto e = s1.finish();
  CHECK_EQ(ac4::hex(e.data(), e.size()), sha1_hex(""));
}

TEST(crypto_wrappers_match_sha) {
  // common/crypto.h is the interface the tools use; whatever backend it picked must agree.
  const std::string msg = "Armored Core 4 — native port";
  auto a = ac4::crypto::sha1(msg.data(), msg.size());
  auto b = ac4::sha::sha1(msg.data(), msg.size());
  CHECK(ac4::hex(a.data(), a.size()) == ac4::hex(b.data(), b.size()));
  auto c = ac4::crypto::sha256(msg.data(), msg.size());
  auto d = ac4::sha::sha256(msg.data(), msg.size());
  CHECK(ac4::hex(c.data(), c.size()) == ac4::hex(d.data(), d.size()));
}

TEST(crypto_file_hash_streams_across_buffers) {
  // Larger than the 1 MiB read buffer, so the file path uses the streaming form.
  auto dir = testing::temp_dir("sha-file");
  auto file = dir / "bytes.bin";
  std::string data;
  data.reserve(2500000);
  for (size_t i = 0; i < 2500000; ++i) data.push_back(char('A' + (i % 23)));
  {
    FILE* f = std::fopen(file.string().c_str(), "wb");
    CHECK(f != nullptr);
    CHECK_EQ(std::fwrite(data.data(), 1, data.size(), f), data.size());
    std::fclose(f);
  }
  CHECK_EQ(ac4::crypto::sha256_file_hex(file.string()), sha256_hex(data));
  CHECK_THROWS(ac4::crypto::sha256_file_hex((dir / "missing.bin").string()));
}
