#include "strings.h"

#include <gtest/gtest.h>

TEST(StringsTest, QuoteString) {
	std::vector<std::pair<std::string, std::string>> inputs{
		{"foo BAR 123", "\"foo BAR 123\""},
		{"\n \b \r", "\"\\n \\b \\r\""},
		{"\x01 \x10 \x1b", "\"\\x01 \\x10 \\x1B\""},
		{" \" \\ ", "\" \\\" \\\\ \""}
	};
	for (auto& input : inputs) {
		auto actual = quote_string(input.first);
		auto expected = input.second;
		EXPECT_EQ(actual, expected);
	}
}

TEST(StringsTest, UnquoteString) {
	std::vector<std::pair<std::string, std::optional<std::string>>> inputs{
		{"\"foo bar\"", "foo bar"},
		{"\"\\n \\r\"", "\n \r"},
		{"\"\\x0A\\x20\\x41\\x2b\"", "\n A+"},
		{"\" \\\" \\\\ \"", " \" \\ "},
		{"", std::nullopt},
		{"unopened\"", std::nullopt},
		{"\"unclosed", std::nullopt},
		{"\"\\z\"", std::nullopt},
		{"\"trailing\\\"", std::nullopt},
		{"\"\\x1thing\"", std::nullopt}
	};
	for (auto& input : inputs) {
		auto actual = unquote_string(input.first);
		auto expected = input.second;
		EXPECT_EQ(actual, expected);
	}
}
