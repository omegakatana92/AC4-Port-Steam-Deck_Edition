#include "common/privacy.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ac4 {
namespace {

#ifdef _WIN32
// The user's profile folder and account name. Windows' environment is UTF-16; logs are UTF-8.
std::string env_utf8(const char* name) {
  wchar_t wname[64];
  if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 64) <= 0) return {};
  wchar_t buf[1024];
  DWORD n = GetEnvironmentVariableW(wname, buf, DWORD(std::size(buf)));
  if (!n || n >= std::size(buf)) return {};
  int len = WideCharToMultiByte(CP_UTF8, 0, buf, int(n), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, buf, int(n), s.data(), len, nullptr, nullptr);
  return s;
}
constexpr const char* kProfileVar = "USERPROFILE";
constexpr const char* kUserVar = "USERNAME";
constexpr const char* kProfileToken = "%USERPROFILE%";
#else
std::string env_utf8(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
}
constexpr const char* kProfileVar = "HOME";
constexpr const char* kUserVar = "USER";
constexpr const char* kProfileToken = "~";
#endif

std::string lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

struct Patterns {
  std::vector<std::string> profiles;  // lower case, both slash styles
  std::string user;                   // lower case
};

const Patterns& patterns() {
  static const Patterns p = [] {
    Patterns r;
    std::string profile = env_utf8(kProfileVar);
    while (!profile.empty() && (profile.back() == '\\' || profile.back() == '/')) profile.pop_back();
    if (profile.size() > 3) {
      std::string back = lower(profile), fwd = back;
      std::replace(fwd.begin(), fwd.end(), '\\', '/');
      r.profiles = {back, fwd};
    }
    r.user = lower(env_utf8(kUserVar));
    if (r.user.size() < 2) r.user.clear();  // a one-letter name would blank out ordinary text
    return r;
  }();
  return p;
}

bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'; }

}  // namespace

std::string redact_personal(const std::string& text) {
  const Patterns& p = patterns();
  if (p.profiles.empty() && p.user.empty()) return text;
  std::string low = lower(text);
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    bool done = false;
    for (const std::string& pat : p.profiles) {
      if (low.compare(i, pat.size(), pat) == 0) {
        out += kProfileToken;
        i += pat.size();
        done = true;
        break;
      }
    }
    if (done) continue;
    if (!p.user.empty() && low.compare(i, p.user.size(), p.user) == 0 && (i == 0 || !word_char(text[i - 1])) &&
        (i + p.user.size() >= text.size() || !word_char(text[i + p.user.size()]))) {
      out += "<user>";
      i += p.user.size();
      continue;
    }
    out += text[i++];
  }
  return out;
}

}  // namespace ac4
