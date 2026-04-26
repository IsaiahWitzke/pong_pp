#pragma once

#include <string>
#include <string_view>

// Returns the SHA-1 hash of `data` as 20 raw bytes (not hex-encoded).
// Implementation follows RFC 3174.
std::string sha1(std::string_view data);
