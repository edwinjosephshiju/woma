// This file forces the MSVC linker to include the C++ standard library
// properly when linking the CPython executable (woma.exe) with llama.lib.
// Without this, compiling a pure C project against a static C++ library 
// using std::regex will result in LNK2001 unresolved external symbols.
#include <regex>
#include <string>

void _woma_dummy_cpp_force_link() {
    std::regex dummy_re(".*");
    std::string dummy_str = "dummy";
}
