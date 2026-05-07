// POSIX-side implementation of src/platform.hpp.
//
// Compiled on every non-Windows target. Path handling is a pass-through
// (POSIX paths are already UTF-8 byte sequences); File wraps a stdio
// FILE*; entry point is plain int main(int, char**) and treats argv as
// already-UTF-8.

#ifndef _WIN32

#include "platform.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace lexcpp::platform {

File::File(File&& o) noexcept : impl_(o.impl_), owns_(o.owns_) {
    o.impl_ = nullptr;
    o.owns_ = false;
}

File& File::operator=(File&& o) noexcept {
    if (this != &o) {
        if (owns_ && impl_) std::fclose(static_cast<std::FILE*>(impl_));
        impl_ = o.impl_;
        owns_ = o.owns_;
        o.impl_ = nullptr;
        o.owns_ = false;
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
    std::string p(path);
    return File(std::fopen(p.c_str(), "rb"), /*owns=*/true);
}

File open_write(std::string_view path) {
    std::string p(path);
    return File(std::fopen(p.c_str(), "wb"), /*owns=*/true);
}

File& stdin_stream()  { static File f(stdin,  false); return f; }
File& stdout_stream() { static File f(stdout, false); return f; }
File& stderr_stream() { static File f(stderr, false); return f; }

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

void log_stderr(std::string_view line) {
    stderr_stream().write_all(line);
}

} // namespace lexcpp::platform

#ifndef LEXCPP_PLATFORM_NO_MAIN
int main(int argc, char** argv) {
    std::vector<std::string_view> v;
    v.reserve(static_cast<std::size_t>(argc) + 1);
    for (int i = 0; i < argc; ++i) v.emplace_back(argv[i]);
    v.emplace_back();   // sentinel
    return core_main(argc, v.data());
}
#endif

#endif // !_WIN32
