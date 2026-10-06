// Runs this test executable again as a second process (a real other writer of
// the settings file), and small process helpers for the D-Bus test.
#pragma once

#include <cstdlib>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace bstest {

inline std::string quote_arg(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// Runs `exe args...` through the shell and returns its exit status (or -1).
inline int run_process(const std::string& exe, const std::vector<std::string>& args) {
    std::string cmd = quote_arg(exe);
    for (const auto& a : args) cmd += " " + quote_arg(a);
#if defined(_WIN32)
    // cmd.exe /c strips one pair of outer quotes when the line starts with one.
    cmd = "\"" + cmd + "\"";
#endif
    int rc = std::system(cmd.c_str());
#if defined(_WIN32)
    return rc;
#else
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
#endif
}

}  // namespace bstest
