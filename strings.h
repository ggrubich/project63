#pragma once

#include <optional>
#include <string>
#include <string_view>

// Converts a string to its string literal representation.
// This conversion includes adding qoutation marks around the string
// as well as replacing non printable characters, quotes and backslashes
// with escape sequences.
std::string quote_string(const std::string_view& str);

// Attempts to parse string from a string literal, on error returns nullopt.
// This is the inverse of quote_string.
std::optional<std::string> unquote_string(const std::string_view& str);
