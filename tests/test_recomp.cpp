// End-to-end recompiler test with a synthetic PowerPC64 ELF: no game data is involved. The image
// holds `li r3,42; blr` and an OPD entry for it; analysis must find the function, and the emitter
// must produce the expected C++. The same test optionally runs the real ac4-recomp binary.
#include "harness.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "common/bytes.h"
#include "recomp/analysis.h"
#include "recomp/emit.h"
#include "recomp/image.h"

namespace fs = std::filesystem;
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

// A minimal big-endian PPC64 executable:
//   .text at 0x10000: li r3,42 ; blr
//   .opd  at 0x10008: { 0x10000, 0 }   (the entry descriptor)
// One PT_LOAD covers both; e_entry is the OPD address.
std::vector<u8> synthetic_elf() {
  constexpr size_t kDataOff = 0x1000;
  constexpr u32 kTextVa = 0x10000;
  constexpr u32 kOpdVa = 0x10008;
  constexpr size_t kShOff = 0x1018;
  std::vector<u8> d(kShOff + 3 * 64, 0);

  d[0] = 0x7f;
  d[1] = 'E';
  d[2] = 'L';
  d[3] = 'F';
  d[4] = 2;  // ELFCLASS64
  d[5] = 2;  // ELFDATA2MSB
  d[6] = 1;  // EV_CURRENT
  put_be16(d, 16, 2);     // e_type = ET_EXEC
  put_be16(d, 18, 0x15);  // e_machine = EM_PPC64
  put_be32(d, 20, 1);     // e_version
  put_be64(d, 24, kOpdVa);  // e_entry: the OPD descriptor
  put_be64(d, 32, 64);      // e_phoff
  put_be64(d, 40, kShOff);  // e_shoff
  put_be16(d, 52, 64);      // e_ehsize
  put_be16(d, 54, 56);      // e_phentsize
  put_be16(d, 56, 1);       // e_phnum
  put_be16(d, 58, 64);      // e_shentsize
  put_be16(d, 60, 3);       // e_shnum
  put_be16(d, 62, 0);       // e_shstrndx

  // Program header: one RWX LOAD covering .text and .opd.
  put_be32(d, 64 + 0, 1);          // PT_LOAD
  put_be32(d, 64 + 4, 7);          // RWX
  put_be64(d, 64 + 8, kDataOff);   // p_offset
  put_be64(d, 64 + 16, kTextVa);   // p_vaddr
  put_be64(d, 64 + 24, kTextVa);   // p_paddr
  put_be64(d, 64 + 32, 0x18);      // p_filesz
  put_be64(d, 64 + 40, 0x18);      // p_memsz
  put_be64(d, 64 + 48, 0x1000);    // p_align

  put_be32(d, kDataOff + 0, 0x3860002au);  // li r3, 42
  put_be32(d, kDataOff + 4, 0x4e800020u);  // blr
  put_be32(d, kDataOff + 8, kTextVa);      // OPD: code address
  put_be32(d, kDataOff + 12, 0);           // OPD: TOC

  auto section = [&](size_t idx, u32 type, u64 flags, u32 addr, u64 off, u64 size, u64 align) {
    const size_t s = kShOff + idx * 64;
    put_be32(d, s + 4, type);
    put_be64(d, s + 8, flags);
    put_be64(d, s + 16, addr);
    put_be64(d, s + 24, off);
    put_be64(d, s + 32, size);
    put_be64(d, s + 48, align);
  };
  section(1, 1, 0x6, kTextVa, kDataOff, 8, 4);  // .text: SHF_ALLOC | SHF_EXECINSTR
  section(2, 1, 0x2, kOpdVa, kDataOff + 8, 8, 8);  // .opd: SHF_ALLOC
  return d;
}

std::string read_text(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

}  // namespace

TEST(synthetic_elf_image_and_analysis) {
  recomp::Image img(synthetic_elf());
  CHECK(img.mapped(0x10000, 4));
  CHECK_EQ(img.r32(0x10000), 0x3860002au);
  CHECK_EQ(img.r32(0x10004), 0x4e800020u);
  CHECK(img.in_code(0x10000));
  CHECK(!img.in_code(0x10008));
  CHECK_EQ(img.entry_opd(), 0x10008u);
  CHECK(!img.is_module());
  CHECK_EQ(img.opd().addr, 0x10008u);
  CHECK_EQ(img.opd_entries().size(), size_t(1));
  CHECK_EQ(img.opd_entries()[0].first, 0x10000u);

  recomp::Analysis an(img);
  an.run();
  const auto& fns = an.functions();
  CHECK_EQ(fns.count(0x10000), size_t(1));
  const recomp::Function& f = fns.at(0x10000);
  CHECK_EQ(f.insns.size(), size_t(2));
  CHECK_EQ(f.invalid, 0u);
  CHECK_EQ(an.stats().functions, size_t(1));
  CHECK_EQ(an.stats().insns, size_t(2));
}

TEST(synthetic_elf_patch_and_strings) {
  recomp::Image img(synthetic_elf());
  img.patch32(0x10000, 0x3860002au, 0x38600063u);  // li r3, 99
  CHECK_EQ(img.r32(0x10000), 0x38600063u);
  CHECK_THROWS(img.patch32(0x10000, 0x3860002au, 0x38600001u));  // expected value does not match
  CHECK_THROWS(img.r32(0x90000000));  // unmapped
  // The text begins 38 60 00 2a: the C string stops at the 00 byte.
  CHECK_EQ(img.cstr(0x10000, 8).size(), size_t(2));
}

TEST(synthetic_elf_emit) {
  recomp::Image img(synthetic_elf());
  recomp::Analysis an(img);
  an.run();
  auto dir = testing::temp_dir("emit");
  recomp::EmitOptions opt;
  opt.comments = true;
  recomp::emit_all({{&img, &an, "ppu"}}, dir.string(), opt);

  CHECK(fs::exists(dir / "ppu_gen_000.cpp"));
  CHECK(fs::exists(dir / "ppu_functab.cpp"));
  CHECK(fs::exists(dir / "gen_info.txt"));
  const std::string body = read_text(dir / "ppu_gen_000.cpp");
  CHECK(body.find("void f_00010000(Ctx& __restrict ctx)") != std::string::npos);
  CHECK(body.find("R(3)") != std::string::npos);
  CHECK(body.find("#include \"ppu/ppu_recomp.h\"") != std::string::npos);

  const std::string tab = read_text(dir / "ppu_functab.cpp");
  CHECK(tab.find("{0x00010000u,f_00010000}") != std::string::npos);
  CHECK(tab.find("g_func_count = 1") != std::string::npos);

  // Emitting again with the same input writes nothing (incremental builds stay cheap).
  const auto before = fs::last_write_time(dir / "ppu_gen_000.cpp");
  recomp::emit_all({{&img, &an, "ppu"}}, dir.string(), opt);
  CHECK(fs::last_write_time(dir / "ppu_gen_000.cpp") == before);
}

TEST(recomp_tool_runs_on_a_synthetic_image) {
  if (testing::tools.recomp.empty()) {
    std::printf("      (skipped: pass --recomp <ac4-recomp> to run the tool)\n");
    return;
  }
  auto dir = testing::temp_dir("recomp-tool");
  const auto elf = dir / "synthetic.elf";
  {
    std::ofstream f(elf, std::ios::binary);
    const auto bytes = synthetic_elf();
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  }
  const auto out = dir / "gen";
  const std::string cmd = "\"" + testing::tools.recomp + "\" emit \"" + elf.string() + "\" \"" + out.string() + "\" > \"" +
                          (dir / "tool.log").string() + "\" 2>&1";
  const int rc = std::system(cmd.c_str());
  CHECK_EQ(rc, 0);
  CHECK(fs::exists(out / "ppu_gen_000.cpp"));
  CHECK(fs::exists(out / "ppu_functab.cpp"));
  CHECK(fs::exists(out / "ppu_gen_001.cpp") == false);

  // The tool's diagnostics name the function it found.
  const std::string log = read_text(dir / "tool.log");
  CHECK(log.find("functions 1") != std::string::npos);
}
