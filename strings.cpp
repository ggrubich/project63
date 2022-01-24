#include "strings.h"

#include <iomanip>
#include <ios>
#include <sstream>

namespace {

constexpr auto escapes = std::array{'a', 'b', 't', 'n', 'v', 'f', 'r'};
constexpr auto specials = std::array{'"', '\\'};

template<size_t N>
std::optional<size_t> find_char(const std::array<char, N>& xs, char x) {
	for (size_t i = 0; i < xs.size(); ++i) {
		if (xs[i] == x) {
			return i;
		}
	}
	return std::nullopt;
}

}  // namespace anonymous

std::string quote_string(const std::string_view& str) {
	std::stringstream buf;
	buf << "\"";
	for (auto c : str) {
		if ('\a' <= c && c <= '\r') {
			buf << '\\' << escapes[c - '\a'];
		}
		else if (find_char(specials, c)) {
			buf << '\\' << c;
		}
		else if (0x00 <= c && c <= 0x1f) {
			buf << "\\x";
			buf << std::setw(2) << std::setfill('0') <<
				std::hex << std::uppercase << int(c);
		}
		else {
			buf << c;
		}
	}
	buf << "\"";
	return buf.str();
}

namespace {

std::optional<uint64_t> parse_hex(const std::string_view& str) {
	uint64_t out = 0;
	for (auto c : str) {
		out *= 16;
		if ('0' <= c && c <= '9') {
			out += c - '0';
		}
		else if ('a' <= c && c <= 'f') {
			out += 10 + c - 'a';
		}
		else if ('A' <= c && c <= 'F') {
			out += 10 + c - 'A';
		}
		else {
			return std::nullopt;
		}
	}
	return out;
}

}  // namespace anonymous

std::optional<std::string> unquote_string(const std::string_view& str) {
	if (str.size() < 2 || str[0] != '"' || str[str.size()-1] != '"') {
		return std::nullopt;
	}
	std::stringstream buf;
	size_t i = 1;
	size_t end = str.size() - 1;
	while (i < end) {
		bool ok = false;
		if (str[i] == '\\' && i+1 < end) {
			if (auto e = find_char(escapes, str[i+1])) {
				buf << char('\a' + *e);
				i += 2;
				ok = true;
			}
			else if (find_char(specials, str[i+1])) {
				buf << str[i+1];
				i += 2;
				ok = true;
			}
			else if (str[i+1] == 'x' && i+3 < end) {
				if (auto hex = parse_hex(str.substr(i+2, 2))) {
					buf << char(*hex);
					i += 4;
					ok = true;
				}
			}
		}
		else if (str[i] != '\\') {
			buf << str[i];
			++i;
			ok = true;
		}
		if (!ok) {
			return std::nullopt;
		}
	}
	return buf.str();
}
