#include "codegen.h"

#include "runtime_template.inc"

namespace lexcpp {

std::string emit_c(const CodegenInput& /*in*/) {
    return std::string(kRuntimeTemplate); // overhauled in Phase C
}

} // namespace lexcpp
