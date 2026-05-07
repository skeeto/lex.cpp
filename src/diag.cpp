#include "diag.hpp"
#include "platform.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace lexcpp {

namespace {

// Format a diagnostic line and ship it through the platform's stderr.
// snprintf is used purely for *formatting* (no I/O); the actual write
// goes through platform::stderr_stream() so the I/O contract that
// "everything in src/ above the platform layer avoids stdio" holds.
void emit(const char* level, const SourceLoc& loc, std::string_view msg) {
    char buf[1024];
    int n;
    if (!loc.file.empty()) {
        n = std::snprintf(buf, sizeof(buf), "%s:%u:%u: %s: %.*s\n",
                          loc.file.c_str(), loc.line, loc.col, level,
                          static_cast<int>(msg.size()), msg.data());
    } else {
        n = std::snprintf(buf, sizeof(buf), "lex: %s: %.*s\n", level,
                          static_cast<int>(msg.size()), msg.data());
    }
    if (n < 0) return;
    std::size_t len = (static_cast<std::size_t>(n) < sizeof(buf))
        ? static_cast<std::size_t>(n) : sizeof(buf) - 1;
    platform::log_stderr(std::string_view(buf, len));
}

} // namespace

void Diagnostics::error(const SourceLoc& loc, std::string msg) {
    ++errors_;
    emit("error", loc, msg);
}

void Diagnostics::warn(const SourceLoc& loc, std::string msg) {
    ++warnings_;
    emit("warning", loc, msg);
}

} // namespace lexcpp
