// Instruction decoding: the PPU decoder (shared by the recompiler and the runtime) and the SPU
// ISA/chunk discovery (shared by the SPU recompiler and the interpreter). These are the two places
// where a Linux build must be bit-identical to the Windows one, so they are tested first.
#include "harness.h"

#include <cstring>
#include <string>
#include <vector>

#include "ppu/ppu_insn.h"
#include "spu/spu_isa.h"

namespace {

std::string ppu_disasm(u32 addr, u32 w) {
  char buf[128];
  ppu::disasm(addr, w, buf, sizeof buf);
  return std::string(buf);
}

std::string spu_disasm(u32 w) { return std::string(spu::op_name(spu::decode(w))); }

u32 spu_word(u32 code_bits, u32 code, u32 immed, u32 rt) {
  return (code << (32 - code_bits)) | (immed << 7) | (rt & 0x7f);
}

}  // namespace

TEST(ppu_decode_common_instructions) {
  // addi r3, r0, 42  ->  li r3, 42
  const u32 li = 0x3860002au;
  CHECK(ppu::decode(li) == ppu::Op::ADDI);
  CHECK_EQ(ppu::rd(li), 3u);
  CHECK_EQ(ppu::ra(li), 0u);
  CHECK_EQ(ppu::simm(li), 42);
  CHECK_EQ(ppu_disasm(0x10000, li), std::string("li r3,42"));

  // blr
  const u32 blr = 0x4e800020u;
  CHECK(ppu::decode(blr) == ppu::Op::BCLR);
  CHECK_EQ(ppu_disasm(0x10004, blr), std::string("blr"));

  // mflr r0
  const u32 mflr = 0x7c0802a6u;
  CHECK(ppu::decode(mflr) == ppu::Op::MFSPR);
  CHECK_EQ(ppu::spr(mflr), 8u);

  // nop (ori r0, r0, 0)
  CHECK(ppu::decode(0x60000000u) == ppu::Op::ORI);
  CHECK_EQ(ppu_disasm(0x10008, 0x60000000u), std::string("nop"));

  // An all-zero word is not an instruction.
  CHECK(ppu::decode(0x00000000u) == ppu::Op::INVALID);

  // lwarx r3, 0, r4 / stwcx.: the reservation pair the guest relies on.
  auto x31 = [](u32 rd, u32 ra, u32 rb, u32 xo, u32 rc) {
    return (31u << 26) | (rd << 21) | (ra << 16) | (rb << 11) | (xo << 1) | rc;
  };
  CHECK(ppu::decode(x31(3, 4, 3, 20, 0)) == ppu::Op::LWARX);
  CHECK(ppu::decode(x31(3, 4, 3, 150, 1)) == ppu::Op::STWCX);
}

TEST(spu_decode_common_instructions) {
  CHECK(spu::decode(0x40200000u) == spu::Op::NOP);
  CHECK(spu::decode(0x00200000u) == spu::Op::LNOP);
  CHECK(spu::decode(0x00000000u) == spu::Op::STOP);
  CHECK(spu::decode(spu_word(11, 0x1A8, 0, 0)) == spu::Op::BI);
  CHECK_EQ(spu_disasm(0x40200000u), std::string("NOP"));
}

TEST(spu_flow_classification) {
  CHECK(spu::flow(spu::Op::NOP) == spu::Flow::Next);
  CHECK(spu::flow(spu::Op::BR) == spu::Flow::Jump);
  CHECK(spu::flow(spu::Op::BRA) == spu::Flow::Jump);
  CHECK(spu::flow(spu::Op::BRZ) == spu::Flow::CondJump);
  CHECK(spu::flow(spu::Op::BRSL) == spu::Flow::Call);
  CHECK(spu::flow(spu::Op::BI) == spu::Flow::Indirect);
  CHECK(spu::flow(spu::Op::BISL) == spu::Flow::IndirectCall);
  CHECK(spu::flow(spu::Op::STOP) == spu::Flow::Stop);
}

TEST(spu_chunk_discovery_and_hash) {
  // A three-instruction chunk: il $3,5 / nop / br back to 0.
  std::vector<u8> ls(spu::kLsSize, 0);
  auto put = [&](u32 pc, u32 w) {
    ls[pc + 0] = u8(w >> 24);
    ls[pc + 1] = u8(w >> 16);
    ls[pc + 2] = u8(w >> 8);
    ls[pc + 3] = u8(w);
  };
  put(0, spu_word(9, 0x081, 5, 3));   // il $3, 5
  put(4, spu_word(11, 0x201, 0, 0));  // nop
  put(8, spu_word(9, 0x064, u32(0xfffe), 0));  // br -2 instructions (back to 0)

  spu::Chunk c = spu::discover_chunk(ls.data(), 0);
  CHECK_EQ(c.entry, 0u);
  CHECK_EQ(c.insns, 3u);
  CHECK_EQ(c.ranges.size(), size_t(1));
  CHECK_EQ(c.ranges[0].first, 0u);
  CHECK_EQ(c.ranges[0].second, 12u);
  const u64 h = spu::chunk_hash(ls.data(), c);
  CHECK_EQ(h, spu::chunk_hash(ls.data(), spu::discover_chunk(ls.data(), 0)));  // deterministic

  // Changing an instruction changes the chunk's identity (the runtime validates chunks by hash).
  put(4, spu_word(9, 0x081, 6, 3));
  CHECK_NE(h, spu::chunk_hash(ls.data(), spu::discover_chunk(ls.data(), 0)));
}

TEST(spu_discover_closure_follows_calls) {
  std::vector<u8> ls(spu::kLsSize, 0);
  auto put = [&](u32 pc, u32 w) {
    ls[pc + 0] = u8(w >> 24);
    ls[pc + 1] = u8(w >> 16);
    ls[pc + 2] = u8(w >> 8);
    ls[pc + 3] = u8(w);
  };
  // 0: brsl to 0x10; 4: nop (return point); 0x10: nop; 0x14: bi $0 (indirect: end)
  put(0, spu_word(9, 0x066, 4, 0));  // brsl (0x10 - 0) / 4 = 4
  put(4, spu_word(11, 0x201, 0, 0));
  put(0x10, spu_word(11, 0x201, 0, 0));
  put(0x14, spu_word(11, 0x1A8, 0, 0));  // bi $0
  auto chunks = spu::discover_closure(ls.data(), 0, 16);
  CHECK(chunks.size() >= 2);
}
