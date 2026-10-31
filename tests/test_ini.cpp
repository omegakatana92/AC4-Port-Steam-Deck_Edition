// common/ini: the settings file model (parse, edit in place, atomic save).
#include "harness.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "common/file.h"
#include "common/ini.h"

namespace fs = std::filesystem;
using ac4::Ini;

TEST(ini_parse_sections_keys_and_comments) {
  Ini ini = Ini::parse(
      "; leading comment\r\n"
      "[Display]\r\n"
      "fps = 60\r\n"
      "# a comment line\r\n"
      "MODE=borderless # trailing comment\r\n"
      "\r\n"
      "[MOUSE]\r\n"
      "sensitivity_x = 1.5\r\n"
      "[display]\r\n"
      "fps = 30\r\n");
  CHECK(ini.has_section("display"));
  CHECK(ini.has_section("Display"));  // section names compare case-insensitively
  CHECK(ini.has_section("mouse"));
  CHECK(!ini.has_section("keyboard"));
  CHECK_EQ(ini.get_or("display", "fps", "?"), std::string("30"));  // the last occurrence wins
  CHECK_EQ(ini.get_or("display", "mode", "?"), std::string("borderless"));
  CHECK_EQ(ini.get_or("mouse", "sensitivity_x", "?"), std::string("1.5"));
  CHECK_EQ(ini.get_or("mouse", "missing", "fallback"), std::string("fallback"));
  CHECK(!ini.get("mouse", "missing").has_value());
}

TEST(ini_edit_in_place_keeps_the_rest) {
  Ini ini = Ini::parse("[a]\nkeep = 1\nchange = old\n\n[b]\nx = y\n");
  ini.set("a", "change", "new");
  ini.set("a", "added", "v");
  ini.set("c", "fresh", "1");
  CHECK_EQ(ini.get_or("a", "change", "?"), std::string("new"));
  CHECK_EQ(ini.get_or("a", "added", "?"), std::string("v"));
  CHECK_EQ(ini.get_or("c", "fresh", "?"), std::string("1"));
  CHECK_EQ(ini.get_or("a", "keep", "?"), std::string("1"));
  CHECK_EQ(ini.get_or("b", "x", "?"), std::string("y"));
  CHECK(ini.remove("a", "added"));
  CHECK(!ini.get("a", "added").has_value());
  CHECK(!ini.remove("a", "added"));
  // Setting the same value changes nothing (no needless rewrite).
  const std::string before = ini.text();
  ini.set("a", "change", "new");
  CHECK_EQ(ini.text(), before);
}

TEST(ini_entries_and_add_line) {
  Ini ini = Ini::parse("[k]\na = 1\nb = 2, 3\n");
  auto e = ini.entries("k");
  CHECK_EQ(e.size(), size_t(2));
  CHECK_EQ(e[0].first, std::string("a"));
  CHECK_EQ(e[1].second, std::string("2, 3"));
  ini.add_line("k", "; explained");
  CHECK(ini.text().find("; explained") != std::string::npos);
}

TEST(ini_save_load_roundtrip) {
  auto dir = testing::temp_dir("ini");
  auto file = dir / "settings.ini";
  {
    Ini ini = Ini::parse("[display]\nfps = 60\nmode = borderless\n");
    ini.save(file);
  }
  CHECK(fs::exists(file));
  CHECK(!fs::exists(fs::path(file) += ".tmp"));  // the temporary is renamed, not left behind
  Ini loaded = Ini::load(file);
  CHECK_EQ(loaded.get_or("display", "fps", "?"), std::string("60"));
  // Saving over an existing file replaces it (POSIX rename / MoveFileExW).
  loaded.set("display", "fps", "30");
  loaded.save(file);
  CHECK_EQ(Ini::load(file).get_or("display", "fps", "?"), std::string("30"));
  // CRLF line endings survive a load/save cycle.
  {
    FILE* f = std::fopen(file.string().c_str(), "wb");
    CHECK(f != nullptr);
    std::fputs("[a]\r\nx = 1\r\n", f);
    std::fclose(f);
  }
  Ini crlf = Ini::load(file);
  CHECK(crlf.text().find("\r\n") != std::string::npos);
  CHECK_EQ(crlf.get_or("a", "x", "?"), std::string("1"));
}

TEST(ini_missing_file_is_empty) {
  auto dir = testing::temp_dir("ini-missing");
  Ini ini = Ini::load(dir / "nope.ini");
  CHECK(ini.empty());
}

TEST(ini_save_refuses_protected_roots) {
  auto dir = testing::temp_dir("ini-protected");
  ac4::add_protected_root(dir);
  Ini ini = Ini::parse("[a]\nx = 1\n");
  CHECK_THROWS(ini.save(dir / "settings.ini"));
  // A sibling path with the protected name as a prefix but no separator is not protected.
  ini.save(dir.parent_path() / (dir.filename().string() + "-sibling.ini"));
}
