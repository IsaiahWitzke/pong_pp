#pragma once

#include <string>
#include <string_view>

namespace util {

// Standard RFC 4648 base64 encoding (with '=' padding).
std::string base64_encode(std::string_view data);

} // namespace util
