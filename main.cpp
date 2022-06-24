#include "json.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/xchar.h> // for u16
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using u64 = std::uint64_t;
using i64 = std::int64_t;
using f64 = double;
using u16 = std::uint16_t;
using u8 = std::uint8_t;

using namespace std::string_view_literals;
namespace fs = std::filesystem;

void usage(char const *progname) {
  std::fprintf(stderr, "USAGE: %s [OPTIONS..]\n", progname);
  std::fputs("OPTIONS:\n", stderr);
  std::fputs(" -h,--help       Show this message and exit.\n", stderr);
  std::fputs(" -C PATH,--compiler=PATH\n\
               Where compiler is located\n\
               (default is $HOME/.cargo/bin/jakt)\n",
             stderr);
}

class PreConditionChecker {
  std::string m_precondition_name = "<unknown precondition>";

protected:
  constexpr void set_precondition_name(std::string name) noexcept {
    m_precondition_name = std::move(name);
  }

public:
  constexpr std::string_view name() const noexcept {
    return m_precondition_name;
  }
  virtual ~PreConditionChecker() {}
  virtual std::optional<std::string_view> perform_check() const noexcept = 0;
};

class CompilerPathChecker : public PreConditionChecker {
  fs::path m_path;

public:
  CompilerPathChecker(std::string_view path) noexcept : m_path(path) {
    std::stringstream ss;
    ss << "compiler path: " << m_path;
    set_precondition_name(ss.str());
  }
  virtual std::optional<std::string_view>
  perform_check() const noexcept override {
    auto const stat = fs::status(m_path);
    if (!fs::exists(stat))
      return "can't find compiler binary"sv;
    if (!fs::is_regular_file(stat))
      return "compiler binary must be a normal executable file"sv;
    static constexpr auto perms_needed =
        fs::perms::others_exec | fs::perms::others_read;
    if ((stat.permissions() & perms_needed) != perms_needed)
      return "cannot use compiler binary due to permissions. "
             "Check that o+w and o+r are there."sv;
    return std::nullopt;
  }
};

// Abstract compiler interface
class Compiler {
  std::string_view m_compiler_path;

public:
  constexpr Compiler(std::string_view compiler_path)
      : m_compiler_path(compiler_path) {}
};

static bool check_single_precondition(const PreConditionChecker &checker) {
  auto errored = false;
  std::cerr << "Checking " << checker.name();
  auto const error = checker.perform_check();
  if (error) {
    std::cerr << "  \x1b[1;31mERROR\x1b[m";
    errored = true;
  } else {
    std::cerr << "  \x1b[1;38;5;2mOK\x1b[m";
  }
  std::cerr << '\n';
  return !errored;
}

static bool check_preconditions(
    std::span<std::unique_ptr<const PreConditionChecker>> checkers) {
  auto errored = false;
  for (auto &checker : checkers) {
    errored |= !check_single_precondition(*checker);
  }
  return !errored;
}

auto main(int const argc, char const *const *const argv,
          char const *const *envp) -> int {

  auto const progname = argv[0];

  std::string compiler_path = "";

  // search for environment variables like HOME
  for (; *envp; ++envp) {
    if (strncmp(*envp, "HOME=", 5) == 0) {
      auto home_dir = *envp + 5; // 5 is offset for <home> (envp == HOME=<home>)
      compiler_path = std::string(home_dir) + "/.cargo/bin/jakt";
      continue;
    }
    // windows: home is USERPROFILE
    if (strncmp(*envp, "USERPROFILE=", 12) == 0) {
      auto home_dir =
          *envp + 12; // 12 is offset for <home> (envp == USERPROFILE=<home>)
      compiler_path = std::string(home_dir) + "\\.cargo\\bin\\jakt.exe";
      continue;
    }
  }

  for (int i = 1; i != argc; ++i) {
    if (std::strcmp(argv[i], "-h") == 0 ||
        std::strcmp(argv[i], "--help") == 0) {
      usage(progname);
      return 0;
    }
    // -C PATH, --compiler PATH
    if (std::strcmp(argv[i], "-C") == 0 ||
        std::strcmp(argv[i], "--compiler") == 0) {
      ++i;
      if (i == argc) {
        std::fprintf(stderr, "error: used %s without an argument.\n",
                     argv[i - 1]);
        usage(progname);
        return 1;
      }
      compiler_path = std::string(argv[i]);
      continue;
    }

    // --compiler=PATH
    if (std::strncmp(argv[i], "--compiler=", 11) == 0) {
      auto const rest = argv[i] + 11;
      compiler_path = std::string(rest);
      continue;
    }
  }

  if (!check_single_precondition(CompilerPathChecker(compiler_path)))
    return 1;

  return 0;
}
