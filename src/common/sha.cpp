#include "common/sha.h"

#include <algorithm>
#include <cstring>

namespace ac4::sha {
namespace {

inline u32 rotr(u32 x, int n) { return (x >> n) | (x << (32 - n)); }
inline u32 rotl(u32 x, int n) { return (x << n) | (x >> (32 - n)); }

inline u32 rd_be32(const u8* p) {
  return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}
inline void wr_be32(u8* p, u32 v) {
  p[0] = u8(v >> 24);
  p[1] = u8(v >> 16);
  p[2] = u8(v >> 8);
  p[3] = u8(v);
}
inline void wr_be64(u8* p, u64 v) {
  wr_be32(p, u32(v >> 32));
  wr_be32(p + 4, u32(v));
}

// Length padding shared by both hashes: 0x80, zeros, then the bit length as a big-endian u64.
// `buf` must hold 128 bytes. One block when the message leaves room for the length, two otherwise
// (the second block starts at buf + 64 and carries only the length).
void pad_block(u8* buf, size_t used, u64 bits, const u8* out[2], size_t* n_out) {
  buf[used] = 0x80;
  if (used + 1 > 56) {
    std::memset(buf + used + 1, 0, 64 - (used + 1));
    out[(*n_out)++] = buf;
    std::memset(buf + 64, 0, 56);
    wr_be64(buf + 64 + 56, bits);
    out[(*n_out)++] = buf + 64;
    return;
  }
  std::memset(buf + used + 1, 0, 56 - (used + 1));
  wr_be64(buf + 56, bits);
  out[(*n_out)++] = buf;
}

constexpr u32 kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

}  // namespace

// ---- SHA-256 ----

void Sha256Stream::block(const u8* p) {
  u32 w[64];
  for (int i = 0; i < 16; ++i) w[i] = rd_be32(p + 4 * i);
  for (int i = 16; i < 64; ++i) {
    u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  u32 a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
  for (int i = 0; i < 64; ++i) {
    u32 s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    u32 ch = (e & f) ^ (~e & g);
    u32 t1 = h + s1 + ch + kSha256K[i] + w[i];
    u32 s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    u32 maj = (a & b) ^ (a & c) ^ (b & c);
    u32 t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
  h_[5] += f;
  h_[6] += g;
  h_[7] += h;
}

Sha256Stream::Sha256Stream() : total_(0), used_(0) {
  h_[0] = 0x6a09e667u;
  h_[1] = 0xbb67ae85u;
  h_[2] = 0x3c6ef372u;
  h_[3] = 0xa54ff53au;
  h_[4] = 0x510e527fu;
  h_[5] = 0x9b05688cu;
  h_[6] = 0x1f83d9abu;
  h_[7] = 0x5be0cd19u;
  std::memset(buf_, 0, sizeof buf_);
}

void Sha256Stream::update(const void* data, size_t len) {
  const u8* p = static_cast<const u8*>(data);
  total_ += len;
  if (used_) {
    size_t n = std::min(len, sizeof buf_ - used_);
    std::memcpy(buf_ + used_, p, n);
    used_ += n;
    p += n;
    len -= n;
    if (used_ < sizeof buf_) return;
    block(buf_);
    used_ = 0;
  }
  while (len >= sizeof buf_) {
    block(p);
    p += sizeof buf_;
    len -= sizeof buf_;
  }
  if (len) {
    std::memcpy(buf_, p, len);
    used_ = len;
  }
}

std::array<u8, 32> Sha256Stream::finish() {
  u8 tmp[128];  // the buffered tail plus padding: at most two blocks
  std::memcpy(tmp, buf_, used_);
  const u8* blocks[2];
  size_t n = 0;
  pad_block(tmp, used_, total_ * 8, blocks, &n);
  for (size_t i = 0; i < n; ++i) block(blocks[i]);
  std::array<u8, 32> out;
  for (int i = 0; i < 8; ++i) wr_be32(out.data() + 4 * i, h_[i]);
  return out;
}

// ---- SHA-1 ----

void Sha1Stream::block(const u8* p) {
  u32 w[80];
  for (int i = 0; i < 16; ++i) w[i] = rd_be32(p + 4 * i);
  for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  u32 a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
  for (int i = 0; i < 80; ++i) {
    u32 f, k;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5a827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ed9eba1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8f1bbcdcu;
    } else {
      f = b ^ c ^ d;
      k = 0xca62c1d6u;
    }
    u32 t = rotl(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = rotl(b, 30);
    b = a;
    a = t;
  }
  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
}

Sha1Stream::Sha1Stream() : total_(0), used_(0) {
  h_[0] = 0x67452301u;
  h_[1] = 0xefcdab89u;
  h_[2] = 0x98badcfeu;
  h_[3] = 0x10325476u;
  h_[4] = 0xc3d2e1f0u;
  std::memset(buf_, 0, sizeof buf_);
}

void Sha1Stream::update(const void* data, size_t len) {
  const u8* p = static_cast<const u8*>(data);
  total_ += len;
  if (used_) {
    size_t n = std::min(len, sizeof buf_ - used_);
    std::memcpy(buf_ + used_, p, n);
    used_ += n;
    p += n;
    len -= n;
    if (used_ < sizeof buf_) return;
    block(buf_);
    used_ = 0;
  }
  while (len >= sizeof buf_) {
    block(p);
    p += sizeof buf_;
    len -= sizeof buf_;
  }
  if (len) {
    std::memcpy(buf_, p, len);
    used_ = len;
  }
}

std::array<u8, 20> Sha1Stream::finish() {
  u8 tmp[128];
  std::memcpy(tmp, buf_, used_);
  const u8* blocks[2];
  size_t n = 0;
  pad_block(tmp, used_, total_ * 8, blocks, &n);
  for (size_t i = 0; i < n; ++i) block(blocks[i]);
  std::array<u8, 20> out;
  for (int i = 0; i < 5; ++i) wr_be32(out.data() + 4 * i, h_[i]);
  return out;
}

// ---- one-shot ----

std::array<u8, 20> sha1(const void* data, size_t len) {
  Sha1Stream s;
  s.update(data, len);
  return s.finish();
}

std::array<u8, 32> sha256(const void* data, size_t len) {
  Sha256Stream s;
  s.update(data, len);
  return s.finish();
}

}  // namespace ac4::sha
