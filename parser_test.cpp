#include "parser.h"

#include <gtest/gtest.h>

TEST(ParserTest, Constants) {
	std::string_view input =
		"12345;\"test string\\n\"";
	auto expected = ExpressionSeq{{
		make_expr<IntExpr>(12345),
		make_expr<StringExpr>("test string\n"),
	}};
	auto actual = parse_expr_seq(input);
	EXPECT_EQ(actual, expected);
}

TEST(ParserTest, Variables) {
	std::string_view input =
		"let x = a = b;"
		"foo_bar?=_BAR123baz";
	auto expected = ExpressionSeq{{
		make_expr<LetExpr>("x", make_expr<AssignExpr>("a", make_expr<VariableExpr>("b"))),
		make_expr<AssignExpr>("foo_bar?", make_expr<VariableExpr>("_BAR123baz"))
	}};
	auto actual = parse_expr_seq(input);
	EXPECT_EQ(actual, expected);
}

TEST(ParserTest, Blocks) {
	std::string_view input =
		"{};"
		"{foo; bar;; 1};"
		"{ {foo}; {1; 2;; {{}}}; }";
	auto expected = ExpressionSeq{{
		make_expr<BlockExpr>(std::vector{make_expr<EmptyExpr>()}),
		make_expr<BlockExpr>(std::vector{
			make_expr<VariableExpr>("foo"),
			make_expr<VariableExpr>("bar"),
			make_expr<EmptyExpr>(),
			make_expr<IntExpr>(1),
		}),
		make_expr<BlockExpr>(std::vector{
			make_expr<BlockExpr>(std::vector{
				make_expr<VariableExpr>("foo")
			}),
			make_expr<BlockExpr>(std::vector{
				make_expr<IntExpr>(1),
				make_expr<IntExpr>(2),
				make_expr<EmptyExpr>(),
				make_expr<BlockExpr>(std::vector{
					make_expr<BlockExpr>(std::vector{make_expr<EmptyExpr>()})
				})
			}),
			make_expr<EmptyExpr>(),
		}),
	}};
	auto actual = parse_expr_seq(input);
	EXPECT_EQ(actual, expected);
}

TEST(ParserTest, Conditionals) {
	std::string_view input =
		"if true {foo; bar};\n"
		"if one { 1 }\n"
		"else if two { 2 }\n"
		"else if three { 3 }\n"
		"else {};\n"
		"while x {\n"
		"  print(x);"
		"  if y { break; }"
		"  else { continue }"
		"}";
	auto expected = ExpressionSeq{{
		make_expr<IfExpr>(
			std::vector{std::pair{
				make_expr<VariableExpr>("true"),
				std::vector{
					make_expr<VariableExpr>("foo"),
					make_expr<VariableExpr>("bar"),
				}
			}},
			std::nullopt
		),
		make_expr<IfExpr>(
			std::vector{
				std::pair{
					make_expr<VariableExpr>("one"),
					std::vector{make_expr<IntExpr>(1)}
				},
				std::pair{
					make_expr<VariableExpr>("two"),
					std::vector{make_expr<IntExpr>(2)}
				},
				std::pair{
					make_expr<VariableExpr>("three"),
					std::vector{make_expr<IntExpr>(3)}
				},
			},
			std::optional{std::vector{
				make_expr<EmptyExpr>()
			}}
		),
		make_expr<WhileExpr>(
			make_expr<VariableExpr>("x"),
			std::vector{
				make_expr<CallExpr>(
					make_expr<VariableExpr>("print"),
					std::vector{make_expr<VariableExpr>("x")}
				),
				make_expr<IfExpr>(
					std::vector{std::pair{
						make_expr<VariableExpr>("y"),
						std::vector{
							make_expr<BreakExpr>(),
							make_expr<EmptyExpr>(),
						}
					}},
					std::optional{std::vector{
						make_expr<ContinueExpr>()
					}}
				),
			}
		)
	}};
	auto actual = parse_expr_seq(input);
	EXPECT_EQ(actual, expected);
}

TEST(ParserTest, TryCatch) {
	std::string_view input =
		"try {\n"
		"  throw \"test\""
		"}\n"
		"catch x {\n"
		"  x;"
		"  true"
		"}";
	auto expected = ExpressionSeq{{
		make_expr<TryExpr>(
			std::vector{
				make_expr<ThrowExpr>(make_expr<StringExpr>("test"))
			},
			"x",
			std::vector{
				make_expr<VariableExpr>("x"),
				make_expr<VariableExpr>("true")
			}
		)
	}};
	auto actual = parse_expr_seq(input);
	EXPECT_EQ(actual, expected);
}

TEST(ParserTest, Procedures) {
	std::string_view input =
		"fn() { return 13; };"
		"fn(x, y, z) { 3 };"
		"method { self@x };"
		"method() { return self };"
		"method(x, y,) { x }";
	auto expected = ExpressionSeq{{
		make_expr<LambdaExpr>(std::vector<std::string>{}, std::vector{
			make_expr<ReturnExpr>(make_expr<IntExpr>(13)),
			make_expr<EmptyExpr>(),
		}),
		make_expr<LambdaExpr>(
			std::vector<std::string>{"x", "y", "z"},
			std::vector{make_expr<IntExpr>(3)}
		),
		make_expr<MethodExpr>(
			std::nullopt,
			std::vector{make_expr<GetPropExpr>(make_expr<VariableExpr>("self"), "x")}
		),
		make_expr<MethodExpr>(
			std::vector<std::string>{},
			std::vector{make_expr<ReturnExpr>(make_expr<VariableExpr>("self"))}
		),
		make_expr<MethodExpr>(
			std::vector<std::string>{"x", "y"},
			std::vector{make_expr<VariableExpr>("x")}
		),
	}};
	auto actual = parse_expr_seq(input);
	EXPECT_EQ(actual, expected);
}

TEST(ParserTest, Operators) {
	std::string_view input =
		"-!foo@bar.baz(x.+(x), y);"
		"self@x = foo() + bar.baz;"
		"foo + -bar - !!baz*boo;"
		"(1 > 2) == (3 >= 4) != false;"
		"x == 10;"
		"void(f(()), g(x, y, z,))";

	auto expected = ExpressionSeq{{
		make_expr<UnaryExpr>("-", make_expr<UnaryExpr>("!", make_expr<CallExpr>(
			make_expr<SendExpr>(
				make_expr<GetPropExpr>(
					make_expr<VariableExpr>("foo"),
					"bar"
				),
				"baz"
			),
			std::vector{
				make_expr<CallExpr>(
					make_expr<SendExpr>(make_expr<VariableExpr>("x"), "+"),
					std::vector{make_expr<VariableExpr>("x")}
				),
				make_expr<VariableExpr>("y"),
			}
		))),

		make_expr<SetPropExpr>(
			make_expr<VariableExpr>("self"),
			"x",
			make_expr<BinaryExpr>(
				"+",
				make_expr<CallExpr>(
					make_expr<VariableExpr>("foo"),
					std::vector<ExpressionPtr>{}
				),
				make_expr<SendExpr>(
					make_expr<VariableExpr>("bar"),
					"baz"
				)
			)
		),

		make_expr<BinaryExpr>(
			"*",
			make_expr<BinaryExpr>(
				"-",
				make_expr<BinaryExpr>(
					"+",
					make_expr<VariableExpr>("foo"),
					make_expr<UnaryExpr>("-", make_expr<VariableExpr>("bar"))
				),
				make_expr<UnaryExpr>("!", make_expr<UnaryExpr>("!",
					make_expr<VariableExpr>("baz")
				))
			),
			make_expr<VariableExpr>("boo")
		),

		make_expr<BinaryExpr>(
			"!=",
			make_expr<BinaryExpr>(
				"==",
				make_expr<BinaryExpr>(
					">",
					make_expr<IntExpr>(1),
					make_expr<IntExpr>(2)
				),
				make_expr<BinaryExpr>(
					">=",
					make_expr<IntExpr>(3),
					make_expr<IntExpr>(4)
				)
			),
			make_expr<VariableExpr>("false")
		),

		make_expr<BinaryExpr>("==", make_expr<VariableExpr>("x"), make_expr<IntExpr>(10)),

		make_expr<CallExpr>(make_expr<VariableExpr>("void"), std::vector{
			make_expr<CallExpr>(
				make_expr<VariableExpr>("f"),
				std::vector{make_expr<EmptyExpr>()}
			),
			make_expr<CallExpr>(
				make_expr<VariableExpr>("g"),
				std::vector{
					make_expr<VariableExpr>("x"),
					make_expr<VariableExpr>("y"),
					make_expr<VariableExpr>("z"),
				}
			),
		}),
	}};
	auto actual = parse_expr_seq(input);
	EXPECT_EQ(actual, expected);
}
