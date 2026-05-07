// Windows-side implementation of src/platform.hpp.
//
// Path conversion goes through MultiByteToWideChar / WideCharToMultiByte
// against CP_UTF8. argv is taken from GetCommandLineW (the Win32 ANSI
// argv from main() can be a lossy codepage decode), parsed with
// CommandLineToArgvW, and re-encoded as UTF-8. stdin/stdout are placed
// in O_BINARY mode so byte-for-byte output through stdout matches the
// POSIX side.
//
// File wraps a stdio FILE* (opened via _wfopen). The flex-style runtime
// emitted by codegen is unaffected; this layer only governs the
// generator binary.

#ifdef _WIN32

#include "platform.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <io.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>     // CommandLineToArgvW

namespace lexcpp::platform {

namespace {

std::wstring utf8_to_wide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), n);
    return out;
}

std::string wide_to_utf8(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(),
                                static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

void ensure_binary_std_streams() {
    static struct Init {
        Init() {
            _setmode(_fileno(stdin),  _O_BINARY);
            _setmode(_fileno(stdout), _O_BINARY);
            _setmode(_fileno(stderr), _O_BINARY);
        }
    } _init;
    (void)_init;
}

} // namespace

File::File(File&& o) noexcept : impl_(o.impl_), owns_(o.owns_) {
    o.impl_ = nullptr; o.owns_ = false;
}
File& File::operator=(File&& o) noexcept {
    if (this != &o) {
        if (owns_ && impl_) std::fclose(static_cast<std::FILE*>(impl_));
        impl_ = o.impl_; owns_ = o.owns_;
        o.impl_ = nullptr; o.owns_ = false;
    }
    return *this;
}
File::~File() {
    if (owns_ && impl_) std::fclose(static_cast<std::FILE*>(impl_));
}

std::size_t File::read(std::span<std::byte> buf) {
    if (!impl_) return 0;
    return std::fread(buf.data(), 1, buf.size(),
                      static_cast<std::FILE*>(impl_));
}
bool File::write_all(std::span<const std::byte> b) {
    if (!impl_) return false;
    auto n = std::fwrite(b.data(), 1, b.size(),
                         static_cast<std::FILE*>(impl_));
    return n == b.size();
}
bool File::write_all(std::string_view s) {
    return write_all({reinterpret_cast<const std::byte*>(s.data()), s.size()});
}
bool File::eof() const noexcept {
    return impl_ ? std::feof(static_cast<std::FILE*>(impl_)) != 0 : true;
}
bool File::error() const noexcept {
    return impl_ ? std::ferror(static_cast<std::FILE*>(impl_)) != 0 : true;
}

File open_read(std::string_view path) {
    auto wp = utf8_to_wide(path);
    return File(_wfopen(wp.c_str(), L"rb"), /*owns=*/true);
}

File open_write(std::string_view path) {
    auto wp = utf8_to_wide(path);
    return File(_wfopen(wp.c_str(), L"wb"), /*owns=*/true);
}

File& stdin_stream()  { ensure_binary_std_streams(); static File f(stdin,  false); return f; }
File& stdout_stream() { ensure_binary_std_streams(); static File f(stdout, false); return f; }
File& stderr_stream() { ensure_binary_std_streams(); static File f(stderr, false); return f; }

std::string slurp(File& f) {
    std::string out;
    std::array<std::byte, 4096> buf{};
    while (true) {
        auto n = f.read(buf);
        if (n == 0) break;
        out.append(reinterpret_cast<const char*>(buf.data()), n);
    }
    return out;
}

void log_stderr(std::string_view line) { stderr_stream().write_all(line); }

} // namespace lexcpp::platform

#ifndef LEXCPP_PLATFORM_NO_MAIN

int main(int /*argc*/, char** /*argv*/) {
    int wargc = 0;
    LPWSTR cmdline = GetCommandLineW();
    LPWSTR* wargv  = CommandLineToArgvW(cmdline, &wargc);
    if (!wargv) return 2;

    std::vector<std::string>      storage;
    storage.reserve(static_cast<std::size_t>(wargc));
    for (int i = 0; i < wargc; ++i)
        storage.emplace_back(lexcpp::platform::wide_to_utf8(wargv[i]));

    std::vector<std::string_view> view;
    view.reserve(static_cast<std::size_t>(wargc) + 1);
    for (auto& s : storage) view.emplace_back(s);
    view.emplace_back();

    LocalFree(wargv);
    return core_main(wargc, view.data());
}

#endif // !LEXCPP_PLATFORM_NO_MAIN

#endif // _WIN32
