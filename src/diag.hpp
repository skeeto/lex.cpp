#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lexcpp {

struct SourceLoc {
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t col = 0;
};

class Diagnostics {
public:
    void error(const SourceLoc& loc, std::string msg);
    void warn (const SourceLoc& loc, std::string msg);

    [[nodiscard]] std::size_t error_count() const noexcept { return errors_; }
    [[nodiscard]] bool ok() const noexcept { return errors_ == 0; }

private:
    std::size_t errors_ = 0;
    std::size_t warnings_ = 0;
};

} // namespace lexcpp
