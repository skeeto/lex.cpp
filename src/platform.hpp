#pragma once

// Platform abstraction. Everything in src/ above this layer speaks
// only UTF-8 std::string / std::span<std::byte> -- no <cstdio>, no
// <iostream>, no native paths. The actual platform-specific I/O lives
// in src/platform_posix.cpp or src/platform_windows.cpp; CMake picks
// one based on WIN32.
//
// Conventions:
// - All paths are UTF-8. The Windows backend converts to wchar_t at
//   the boundary via MultiByteToWideChar.
// - argv reaches core_main as UTF-8 std::string_views; the Windows
//   backend uses GetCommandLineW + CommandLineToArgvW + WideCharToMultiByte.
// - File is move-only; std streams are leased from singletons and
//   never closed.
//
// Note: this layer governs the *generator* (bin/lex). The generated
// scanner runtime keeps using <stdio.h> because flex's published
// contract is FILE *yyin / yyout / int yywrap(void); changing it
// would break drop-in compatibility for every existing .l.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace lexcpp::platform {

class File {
public:
    File() noexcept = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) noexcept;
    File& operator=(File&&) noexcept;
    ~File();

    [[nodiscard]] bool ok() const noexcept { return impl_ != nullptr; }

    // Read up to buf.size() bytes. 0 means EOF; partial reads are
    // possible. Sets eof()/error() accordingly.
    std::size_t read(std::span<std::byte> buf);

    // Write the entire span; returns false on partial write or error.
    bool write_all(std::span<const std::byte> bytes);
    bool write_all(std::string_view text);

    [[nodiscard]] bool eof()   const noexcept;
    [[nodiscard]] bool error() const noexcept;

    // Used by platform_*.cpp impls only.
    explicit File(void* impl, bool owns) noexcept : impl_(impl), owns_(owns) {}

private:
    void* impl_ = nullptr;
    bool  owns_ = false;
};

[[nodiscard]] File open_read (std::string_view utf8_path);
[[nodiscard]] File open_write(std::string_view utf8_path);

File& stdin_stream();
File& stdout_stream();
File& stderr_stream();

// Read all of f into a string.
std::string slurp(File& f);

// Format & write a single diagnostic line to stderr_stream(). Equivalent
// to fprintf-with-newline; bounded to a 4 KiB internal buffer.
void log_stderr(std::string_view line);

} // namespace lexcpp::platform

// User-supplied core entry. Receives UTF-8 argv (argc + 1 entries; the
// last is null-terminated by the platform layer).
int core_main(int argc, const std::string_view* argv);
