// The shared settings schema: defaults, display round-trips, and the migrations that must keep
// working on Linux exactly as on Windows (this file is the launcher's and the game's contract).
#include "harness.h"

#include <filesystem>
#include <string>
#include <vector>

#include "common/settings_file.h"

namespace fs = std::filesystem;
using namespace ac4::settings;

TEST(settings_creates_defaults) {
  auto dir = testing::temp_dir("settings");
  auto file = file_in(dir);
  std::vector<std::string> notes;
  ac4::Ini ini = load_or_create(file, &notes);
  CHECK(fs::exists(file));
  for (const char* s : {"display", "mouse", "keyboard", "menu"}) CHECK(ini.has_section(s));
  Display d = read_display(ini);
  CHECK_EQ(d.fps, 60.0);
  CHECK(d.mode == Mode::Borderless);
  CHECK_EQ(d.width, 1280);
  CHECK_EQ(d.height, 720);
  CHECK_EQ(d.render_scale, 100);
  CHECK_EQ(d.pacing, std::string("auto"));
  CHECK_EQ(d.monitor, std::string());
  CHECK_EQ(ini.get_or("keyboard", "lstick_up", "?"), std::string("W"));
  CHECK_EQ(ini.get_or("menu", "cross", "?"), std::string("Enter, Space"));
}

TEST(settings_reload_is_stable) {
  auto dir = testing::temp_dir("settings-reload");
  auto file = file_in(dir);
  load_or_create(file, nullptr);
  const std::string first = ac4::Ini::load(file).text();
  std::vector<std::string> notes;
  load_or_create(file, &notes);
  CHECK(notes.empty());  // nothing was added: a reload changes nothing
  CHECK_EQ(ac4::Ini::load(file).text(), first);
}

TEST(settings_player_edits_survive) {
  auto dir = testing::temp_dir("settings-edit");
  auto file = file_in(dir);
  {
    ac4::Ini ini = load_or_create(file, nullptr);
    ini.set("keyboard", "lstick_up", "Z");
    ini.set("display", "fps", "30");
    ini.save(file);
  }
  ac4::Ini ini = load_or_create(file, nullptr);
  CHECK_EQ(ini.get_or("keyboard", "lstick_up", "?"), std::string("Z"));
  CHECK_EQ(read_display(ini).fps, 30.0);
}

TEST(settings_display_roundtrip) {
  ac4::Ini ini = ac4::Ini::parse("[display]\n");
  Display d;
  d.fps = 0;  // the display's refresh rate
  d.pacing = "clock";
  d.mode = Mode::Fullscreen;
  d.width = 1280;
  d.height = 800;
  d.fs_width = 1920;
  d.fs_height = 1080;
  d.fs_hz = 60;
  d.render_scale = 150;
  d.monitor = "\\\\.\\DISPLAY2";
  write_display(ini, d);
  Display r = read_display(ini);
  CHECK_EQ(r.fps, 0.0);
  CHECK_EQ(r.pacing, std::string("clock"));
  CHECK(r.mode == Mode::Fullscreen);
  CHECK_EQ(r.width, 1280);
  CHECK_EQ(r.height, 800);
  CHECK_EQ(r.fs_width, 1920);
  CHECK_EQ(r.fs_hz, 60);
  CHECK_EQ(r.render_scale, 150);
  CHECK_EQ(r.monitor, std::string("\\\\.\\DISPLAY2"));
  CHECK_EQ(fullscreen_text(r), std::string("1920x1080@60"));
}

TEST(settings_display_validation_and_text) {
  ac4::Ini ini = ac4::Ini::parse(
      "[display]\n"
      "fps = nonsense\n"
      "mode = sideways\n"
      "window = 10x10\n"
      "render_scale = 900\n"
      "fullscreen_mode = 99x99\n");
  Display d = read_display(ini);
  CHECK_EQ(d.fps, 60.0);        // out of range: the default
  CHECK(d.mode == Mode::Windowed);  // unknown: the default
  CHECK_EQ(d.width, 1280);      // too small: the default
  CHECK_EQ(d.render_scale, 100);
  CHECK_EQ(d.fs_width, 0);      // invalid mode: "desktop"
  CHECK_EQ(fullscreen_text(d), std::string("desktop"));
  CHECK_EQ(fps_text(60), std::string("60"));
  CHECK_EQ(fps_text(0), std::string("display"));
  CHECK_EQ(fps_text(kFpsUncapped), std::string("uncapped"));
  CHECK_EQ(fps_text(59.94), std::string("59.94"));
  CHECK_EQ(std::string(mode_name(Mode::Borderless)), std::string("borderless"));
}

TEST(settings_migrates_old_layout) {
  // Before the launcher: fps/pacing/fullscreen at the top level, bindings in input.ini beside it.
  auto dir = testing::temp_dir("settings-old");
  auto file = file_in(dir);
  fs::create_directories(file.parent_path());
  {
    FILE* f = std::fopen(file.string().c_str(), "wb");
    CHECK(f != nullptr);
    std::fputs("fps = 30\npacing = clock\nfullscreen = 1\n", f);
    std::fclose(f);
  }
  {
    FILE* f = std::fopen((file.parent_path() / "input.ini").string().c_str(), "wb");
    CHECK(f != nullptr);
    std::fputs("lstick_up = K\n[mouse]\nmode = camera\n", f);
    std::fclose(f);
  }
  std::vector<std::string> notes;
  ac4::Ini ini = load_or_create(file, &notes);
  Display d = read_display(ini);
  CHECK_EQ(d.fps, 30.0);
  CHECK_EQ(d.pacing, std::string("clock"));
  CHECK(d.mode == Mode::Borderless);
  CHECK_EQ(ini.get_or("keyboard", "lstick_up", "?"), std::string("K"));  // the key name keeps its case
  CHECK_EQ(ini.get_or("mouse", "mode", "?"), std::string("camera"));
  CHECK(fs::exists(fs::path(file) += ".migrated"));
  CHECK(fs::exists(file.parent_path() / "input.ini.migrated"));
  CHECK(!fs::exists(file.parent_path() / "input.ini"));
  CHECK(!notes.empty());
}

TEST(settings_fps_choices_cover_the_release_targets) {
  bool has30 = false, has60 = false, hasDisplay = false;
  for (const auto& c : fps_choices()) {
    has30 |= c.fps == 30;
    has60 |= c.fps == 60;
    hasDisplay |= c.fps == 0;
  }
  CHECK(has30);
  CHECK(has60);
  CHECK(hasDisplay);
}
