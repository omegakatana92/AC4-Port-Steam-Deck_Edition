#include "harness.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <random>

namespace testing {

Tools tools;

std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

void fail(const char* file, int line, const std::string& what) {
  throw Failure{std::string(file) + ":" + std::to_string(line) + ": " + what};
}

std::filesystem::path temp_dir(const char* name) {
  static std::mt19937_64 rng{std::random_device{}()};
  std::filesystem::path root = std::filesystem::temp_directory_path() / "ac4-tests";
  std::filesystem::path dir = root / (std::string(name) + "-" + std::to_string(rng()));
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

namespace {

// The tests' work directories, removed at the end of the run (a failed run keeps them for
// inspection when AC4_KEEP_TEST_DIRS is set).
struct TempRoot {
  std::filesystem::path path = std::filesystem::temp_directory_path() / "ac4-tests";
  ~TempRoot() {
    if (std::getenv("AC4_KEEP_TEST_DIRS")) return;
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

}  // namespace

int run_all(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    if (a == "--recomp") tools.recomp = next();
    else if (a == "--prep") tools.prep = next();
    else if (a == "--spurecomp") tools.spurecomp = next();
    else {
      std::fprintf(stderr, "unknown argument %s\n", a.c_str());
      return 2;
    }
  }
  TempRoot root;
  std::error_code ec;
  std::filesystem::create_directories(root.path, ec);

  int failed = 0, skipped = 0;
  for (const Case& c : registry()) {
    try {
      c.fn();
      std::printf("ok    %s\n", c.name);
    } catch (const Failure& f) {
      std::printf("FAIL  %s\n      %s\n", c.name, f.what.c_str());
      ++failed;
    } catch (const std::exception& e) {
      std::printf("FAIL  %s\n      unexpected exception: %s\n", c.name, e.what());
      ++failed;
    } catch (...) {
      std::printf("FAIL  %s\n      unexpected exception\n", c.name);
      ++failed;
    }
  }
  (void)skipped;
  std::printf("\n%zu test(s), %d failed\n", registry().size(), failed);
  return failed ? 1 : 0;
}

}  // namespace testing

int main(int argc, char** argv) { return testing::run_all(argc, argv); }
