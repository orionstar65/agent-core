#pragma once

#include <string>

namespace agent {
namespace util {

// Resolve a file path to its absolute, canonical form
// Returns empty string on failure
std::string resolve_path(const std::string& path);

// Get the directory containing the executable
// Returns empty string on failure
std::string get_executable_directory();

}  // namespace util
}  // namespace agent

