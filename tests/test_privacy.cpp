// Log privacy: the user's home folder and account name are replaced before logs are shared.
// The pattern table is built once, from the environment, on the first call.
#include "harness.h"

#include <cstdlib>
#include <string>

#include "common/privacy.h"

TEST(privacy_redacts_home_and_user) {
#ifdef _WIN32
  const char* home = std::getenv("USERPROFILE");
  const char* user = std::getenv("USERNAME");
#else
  const char* home = std::getenv("HOME");
  const char* user = std::getenv("USER");
#endif
  if (!home || !user) {
    std::printf("      (skipped: no home/user environment)\n");
    return;
  }
  const std::string text = std::string("open ") + home + "/games/ac4/eboot.elf by " + user + " at 12:00";
  const std::string redacted = ac4::redact_personal(text);
  CHECK(redacted.find(home) == std::string::npos);
  CHECK(redacted.find(user) == std::string::npos);
  CHECK(redacted.find("<user>") != std::string::npos);
  CHECK(redacted.find("eboot.elf") != std::string::npos);  // the rest is untouched

  // Text without personal data is returned unchanged.
  const std::string plain = "guest trap at 0x00100000";
  CHECK_EQ(ac4::redact_personal(plain), plain);
}
