#include "diag.h"

#include <cstdio>
#include <utility>

namespace lexcpp {

namespace {

void emit(const char* level, const SourceLoc& loc, std::string_view msg) {
    if (!loc.file.empty()) {
        std::fprintf(stderr, "%s:%u:%u: %s: %.*s\n",
                     loc.file.c_str(), loc.line, loc.col, level,
                     static_cast<int>(msg.size()), msg.data());
    } else {
        std::fprintf(stderr, "lex: %s: %.*s\n", level,
                     static_cast<int>(msg.size()), msg.data());
    }
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
