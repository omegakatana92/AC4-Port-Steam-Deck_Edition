// End-to-end SPU pipeline on Linux with synthetic data: a manifest whose source is a made-up
// "eboot.elf" -> ac4-prep rebuilds a .spurec record -> ac4-spurecomp compiles it to C++.
// No game code or game data is involved; every byte comes from this test.
#include "harness.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "common/bytes.h"
#include "common/crypto.h"

namespace fs = std::filesystem;

namespace {

void write_bytes(const fs::path& p, const std::vector<unsigned char>& bytes) {
  std::ofstream f(p, std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

std::string read_text(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

// Local-store bytes at offset 0: il $3,5 ; nop ; br back to 0.
std::vector<unsigned char> spu_code_image() {
  std::vector<unsigned char> d(0x200, 0);
  auto put = [&](size_t off, unsigned w) {
    d[off + 0] = u8(w >> 24);
    d[off + 1] = u8(w >> 16);
    d[off + 2] = u8(w >> 8);
    d[off + 3] = u8(w);
  };
  put(0x100, (0x081u << 23) | (5u << 7) | 3u);        // il $3, 5
  put(0x104, (0x201u << 21));                          // nop
  put(0x108, (0x064u << 23) | (0xFFFEu << 7));          // br -2 (back to 0)
  return d;
}

int run_quiet(const std::string& cmd) { return std::system(cmd.c_str()); }

}  // namespace

TEST(spu_pipeline_manifest_records_and_recompiler) {
  if (testing::tools.prep.empty() || testing::tools.spurecomp.empty()) {
    std::printf("      (skipped: pass --prep and --spurecomp)\n");
    return;
  }
  auto dir = testing::temp_dir("spu-pipeline");
  auto prepared = dir / "prepared";
  auto records = dir / "records";
  auto gen = dir / "gen";
  fs::create_directories(prepared);

  const auto code = spu_code_image();
  write_bytes(prepared / "eboot.elf", code);
  const std::string sha = ac4::crypto::sha256_file_hex((prepared / "eboot.elf").string());

  // The manifest is positions only: one source, one record with entry 0 and range 0..12 at 0x100.
  {
    std::ofstream f(dir / "spu_manifest.txt");
    f << "# synthetic test manifest\n";
    f << "source 0 eboot.elf " << sha << "\n";
    f << "record test 0x0 0x0:0xc:0:0x100\n";
  }

  const std::string prep_cmd = "\"" + testing::tools.prep + "\" spu-records --manifest \"" +
                               (dir / "spu_manifest.txt").string() + "\" --out \"" + records.string() + "\" \"" +
                               prepared.string() + "\" > \"" + (dir / "prep.log").string() + "\" 2>&1";
  CHECK_EQ(run_quiet(prep_cmd), 0);
  CHECK(fs::exists(records / "test.spurec"));
  const std::string rec = read_text(records / "test.spurec");
  CHECK_EQ(rec.size(), size_t(16 + 4 + 8 + 12));  // header, entry list, range list, 12 bytes of code
  CHECK_EQ(rec.compare(0, 7, "SPUREC1"), 0);

  const std::string spu_cmd = "\"" + testing::tools.spurecomp + "\" \"" + gen.string() + "\" \"" + records.string() +
                              "\" > \"" + (dir / "spurecomp.log").string() + "\" 2>&1";
  CHECK_EQ(run_quiet(spu_cmd), 0);
  CHECK(fs::exists(gen / "spu_gen_000.cpp"));
  CHECK(fs::exists(gen / "spu_chunks.cpp"));
  const std::string body = read_text(gen / "spu_gen_000.cpp");
  CHECK(body.find("bool spu_c_") != std::string::npos);
  const std::string table = read_text(gen / "spu_chunks.cpp");
  CHECK(table.find("g_chunk_count = 1u") != std::string::npos);

  // An unchanged record is not rewritten (incremental preparation is cheap).
  CHECK_EQ(run_quiet(prep_cmd), 0);
  CHECK(read_text(dir / "prep.log").find("0 written, 1 unchanged") != std::string::npos);
}
