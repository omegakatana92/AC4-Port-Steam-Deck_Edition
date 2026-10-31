// Minimal self-registering test harness. No external dependency: the port builds on machines with
// nothing installed but a compiler, and the tests must never need game data.
#pragma once
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace testing {

struct Case {
  const char* name;
  void (*fn)();
};

std::vector<Case>& registry();

struct Registrar {
  Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

// Thrown by a failed CHECK: aborts the current test only.
struct Failure {
  std::string what;
};

[[noreturn]] void fail(const char* file, int line, const std::string& what);

// Tool paths given on the command line (empty: the test is skipped). Used by the end-to-end test.
struct Tools {
  std::string recomp, prep, spurecomp;
};
extern Tools tools;

// A fresh empty directory below the system temporary directory; removed when the run ends.
std::filesystem::path temp_dir(const char* name);

int run_all(int argc, char** argv);

}  // namespace testing

#define TEST(name)                                              \
  static void test_##name();                                    \
  static ::testing::Registrar registrar_##name(#name, test_##name); \
  static void test_##name()

#define CHECK(cond)                                                                    \
  do {                                                                                 \
    if (!(cond)) ::testing::fail(__FILE__, __LINE__, "CHECK failed: " #cond);          \
  } while (0)

#define CHECK_EQ(a, b)                                                                     \
  do {                                                                                     \
    if (!((a) == (b))) ::testing::fail(__FILE__, __LINE__, "CHECK_EQ failed: " #a " == " #b); \
  } while (0)

#define CHECK_NE(a, b)                                                                     \
  do {                                                                                     \
    if (!((a) != (b))) ::testing::fail(__FILE__, __LINE__, "CHECK_NE failed: " #a " != " #b); \
  } while (0)

#define CHECK_THROWS(expr)                                                                      \
  do {                                                                                          \
    bool threw_ = false;                                                                        \
    try {                                                                                       \
      (void)(expr);                                                                             \
    } catch (const ::testing::Failure&) {                                                       \
      throw;                                                                                    \
    } catch (...) {                                                                             \
      threw_ = true;                                                                            \
    }                                                                                           \
    if (!threw_) ::testing::fail(__FILE__, __LINE__, "CHECK_THROWS failed: " #expr " did not throw"); \
  } while (0)
