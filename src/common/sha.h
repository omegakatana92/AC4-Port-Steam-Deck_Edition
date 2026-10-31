// SHA-1 and SHA-256, portable C++.
//
// The port verifies the player's own files with these hashes (common/crypto.h). Windows can use
// CNG (bcrypt) instead; this implementation is what the Linux build uses and what the tests check
// against the NIST vectors, so both paths must produce identical digests.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "common/bytes.h"

namespace ac4::sha {

std::array<u8, 20> sha1(const void* data, size_t len);
std::array<u8, 32> sha256(const void* data, size_t len);

// Streaming form (used for files: the input is larger than memory).
class Sha1Stream {
 public:
  Sha1Stream();
  void update(const void* data, size_t len);
  std::array<u8, 20> finish();

 private:
  u32 h_[5];
  u64 total_;
  u8 buf_[64];
  size_t used_;
  void block(const u8* p);
};

class Sha256Stream {
 public:
  Sha256Stream();
  void update(const void* data, size_t len);
  std::array<u8, 32> finish();

 private:
  u32 h_[8];
  u64 total_;
  u8 buf_[64];
  size_t used_;
  void block(const u8* p);
};

}  // namespace ac4::sha
