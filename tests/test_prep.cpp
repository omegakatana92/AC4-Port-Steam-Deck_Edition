// Preparation helpers that are pure code (no user files needed): PRX relocation and the prep-side
// PARAM.SFO reader. A synthetic PRX verifies the relocated image's layout and headers.
#include "harness.h"

#include <string>
#include <vector>

#include "common/bytes.h"
#include "common/sfo.h"
#include "prep/formats.h"

using namespace ac4;

namespace {

void put_be16(std::vector<u8>& d, size_t off, u16 v) {
  d[off + 0] = u8(v >> 8);
  d[off + 1] = u8(v);
}
void put_be32(std::vector<u8>& d, size_t off, u32 v) {
  d[off + 0] = u8(v >> 24);
  d[off + 1] = u8(v >> 16);
  d[off + 2] = u8(v >> 8);
  d[off + 3] = u8(v);
}
void put_be64(std::vector<u8>& d, size_t off, u64 v) {
  put_be32(d, off, u32(v >> 32));
  put_be32(d, off + 4, u32(v));
}

// A PRX with one 0x20-byte LOAD segment and no relocations.
std::vector<u8> synthetic_prx() {
  constexpr size_t kSegOff = 0x100;
  std::vector<u8> d(kSegOff + 0x20, 0);
  d[0] = 0x7f;
  d[1] = 'E';
  d[2] = 'L';
  d[3] = 'F';
  d[4] = 2;
  d[5] = 2;
  d[6] = 1;
  put_be16(d, 16, 0xFFA4);  // e_machine of a PPU PRX
  put_be64(d, 32, 0x40);    // e_phoff
  put_be16(d, 54, 56);      // e_phentsize
  put_be16(d, 56, 1);       // e_phnum
  // Program header: LOAD at file offset 0x100, paddr 0x100, 0x20 bytes.
  put_be32(d, 0x40 + 0, 1);
  put_be32(d, 0x40 + 4, 5);  // PF_R | PF_X
  put_be64(d, 0x40 + 8, kSegOff);
  put_be64(d, 0x40 + 16, 0x1000);
  put_be64(d, 0x40 + 24, kSegOff);
  put_be64(d, 0x40 + 32, 0x20);
  put_be64(d, 0x40 + 40, 0x20);
  put_be64(d, 0x40 + 48, 4);
  for (size_t i = 0; i < 0x20; ++i) d[kSegOff + i] = u8(0xC0 + i);
  return d;
}

}  // namespace

TEST(prx_relocation_layout) {
  const std::vector<u8> prx = synthetic_prx();
  constexpr u32 kBase = 0x0A000000;
  std::string log;
  std::vector<u8> elf = prep::relocate_prx(prx, kBase, &log);
  CHECK(!log.empty());

  // ELF64 big-endian header, two program headers (LOAD + module-info marker).
  CHECK_EQ(rd_be32(&elf[0]), 0x7F454C46u);
  CHECK_EQ(elf[4], u8(2));
  CHECK_EQ(elf[5], u8(2));
  CHECK_EQ(rd_be64(&elf[32]), u64(0x40));  // e_phoff
  CHECK_EQ(rd_be16(&elf[54]), u16(56));
  CHECK_EQ(rd_be16(&elf[56]), u16(2));  // e_phnum
  CHECK_EQ(rd_be16(&elf[60]), u16(0));  // e_shnum
  CHECK_EQ(rd_be64(&elf[24]), u64(0));  // e_entry

  // LOAD segment: mapped to the base, data at file offset 0x1000.
  const size_t ph0 = 0x40;
  CHECK_EQ(rd_be32(&elf[ph0]), 1u);
  CHECK_EQ(rd_be32(&elf[ph0 + 4]), 5u);
  CHECK_EQ(rd_be64(&elf[ph0 + 8]), u64(0x1000));  // p_offset
  CHECK_EQ(u32(rd_be64(&elf[ph0 + 16])), kBase);  // p_vaddr
  CHECK_EQ(u32(rd_be64(&elf[ph0 + 32])), 0x20u);  // p_filesz
  CHECK_EQ(rd_be64(&elf[ph0 + 48]), u64(0x10000));

  // Module-info marker (the recompiler and runtime find the module info through it).
  const size_t ph1 = 0x40 + 56;
  CHECK_EQ(rd_be32(&elf[ph1]), 0x6F000001u);
  CHECK_EQ(u32(rd_be64(&elf[ph1 + 16])), kBase);  // p_vaddr = module info address

  // The segment's bytes moved to 0x1000 unchanged.
  CHECK_EQ(elf.size(), size_t(0x1000 + 0x10000));
  for (size_t i = 0; i < 0x20; ++i) CHECK_EQ(elf[0x1000 + i], u8(0xC0 + i));
}

TEST(prx_rejects_non_prx) {
  std::vector<u8> junk(128, 0);
  std::string log;
  CHECK_THROWS(prep::relocate_prx(junk, 0x0A000000, &log));
  std::vector<u8> elf(128, 0);
  elf[0] = 0x7f;
  elf[1] = 'E';
  elf[2] = 'L';
  elf[3] = 'F';
  put_be16(elf, 16, 0x15);  // a plain PPC64 executable, not a PRX
  CHECK_THROWS(prep::relocate_prx(elf, 0x0A000000, &log));
}

TEST(prep_sfo_text) {
  sfo::Map m;
  sfo::set_string(m, "TITLE_ID", "BLUS30027", 12);
  sfo::set_string(m, "TITLE", "Armored Core 4", 64);
  sfo::set_int(m, "PARENTAL_LEVEL", 5);
  const auto bytes = sfo::build(m);
  const auto map = prep::parse_sfo(bytes);
  const std::string text = prep::sfo_to_text(map);
  CHECK(text.find("BLUS30027") != std::string::npos);
  CHECK(text.find("Armored Core 4") != std::string::npos);
  CHECK(text.find("PARENTAL_LEVEL") != std::string::npos);
}
