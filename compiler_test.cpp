#include "compiler.h"
#include "vm.h"

#include <functional>

#include <gtest/gtest.h>

template<typename F>
Root<Ptr<CppFunction>> make_binary(Context& ctx, F func) {
	return ctx.alloc(CppLambda(2, [=](Context& ctx, const std::vector<Value>& xs) {
		auto x = xs[0].get<int64_t>();
		auto y = xs[1].get<int64_t>();
		return ctx.root<Value>(func(x, y));
	}));
}

void init_builtins(Context& ctx) {
	ctx.builtins["true"] = Value(true);
	ctx.builtins["=="] = *make_binary(ctx, std::equal_to<int64_t>{});
	ctx.builtins["!="] = *make_binary(ctx, std::not_equal_to<int64_t>{});
	ctx.builtins["+"] = *make_binary(ctx, std::plus<int64_t>{});
	ctx.builtins["-"] = *make_binary(ctx, std::minus<int64_t>{});
}

TEST(CompilerTest, RecursiveFunctions) {
	// fn main() {
	//     ping(10)
	// }
	//
	// fn ping(n) {
	//     if n == 0 {
	//         return 0;
	//     };
	//     let n = n - 1;
	//     let y = pong(n);
	//     return y + y;
	// }
	//
	// fn pong(n) {
	//     if 0 == n { 1 } else { ping(n - 1) + 1 }
	// }
	//
	// main()

	Context ctx;
	init_builtins(ctx);

	auto body = ExpressionSeq{{
		make_expr<LetExpr>("main", make_expr<LambdaExpr>(
			std::vector<std::string>{},
			std::vector{
				make_expr<CallExpr>(
					make_expr<VariableExpr>("ping"),
					std::vector{make_expr<IntExpr>(10)}
				)
			}
		)),

		make_expr<LetExpr>("ping", make_expr<LambdaExpr>(
			std::vector<std::string>{"n"},
			std::vector{

				make_expr<IfExpr>(
					std::vector{std::pair{
						make_expr<CallExpr>(
							make_expr<VariableExpr>("=="),
							std::vector{
								make_expr<VariableExpr>("n"),
								make_expr<IntExpr>(0)
							}
						),
						std::vector{
							make_expr<ReturnExpr>(make_expr<IntExpr>(0))
						}
					}},
					std::nullopt
				),

				make_expr<LetExpr>("n", make_expr<CallExpr>(
					make_expr<VariableExpr>("-"),
					std::vector{make_expr<VariableExpr>("n"), make_expr<IntExpr>(1)}
				)),

				make_expr<LetExpr>("y", make_expr<CallExpr>(
					make_expr<VariableExpr>("pong"),
					std::vector{make_expr<VariableExpr>("n")}
				)),

				make_expr<ReturnExpr>(
					make_expr<CallExpr>(
						make_expr<VariableExpr>("+"),
						std::vector{
							make_expr<VariableExpr>("y"),
							make_expr<VariableExpr>("y")
						}
					)
				)

			}
		)),

		make_expr<LetExpr>("pong", make_expr<LambdaExpr>(
			std::vector<std::string>{"n"},
			std::vector{

				make_expr<IfExpr>(
					std::vector{std::pair{
						make_expr<CallExpr>(
							make_expr<VariableExpr>("=="),
							std::vector{
								make_expr<IntExpr>(0),
								make_expr<VariableExpr>("n")
							}
						),
						std::vector{
							make_expr<IntExpr>(1)
						}
					}},
					std::optional{std::vector{
						make_expr<CallExpr>(
							make_expr<VariableExpr>("+"),
							std::vector{
								make_expr<CallExpr>(
									make_expr<VariableExpr>("ping"),
									std::vector{
										make_expr<CallExpr>(
											make_expr<VariableExpr>("-"),
											std::vector{
												make_expr<VariableExpr>("n"),
												make_expr<IntExpr>(1)
											}
										)
									}
								),
								make_expr<IntExpr>(1)
							}
						)
					}}
				)

			}
		)),

		make_expr<CallExpr>(
			make_expr<VariableExpr>("main"),
			std::vector<ExpressionPtr>{}
		)
	}};

	auto compiler = ctx.root(Compiler(ctx));
	auto main = compiler->compile(body);
	auto vm = ctx.root(VM(ctx));
	auto result = vm->run(*main)->get<int64_t>();
	EXPECT_EQ(result, 62) << "Evaluation result is wrong";
}

TEST(CompilerTest, TryCatch) {
	// try {
	//     try {
	//	       let x = 2;
	//         throw x;
	//         x = 0;
	//         return x;
	//     }
	//     catch x {
	//		   let y = x + 1;
	//         y
	//     }
	// }
	// catch _ {
	//     return 0;
	// }

	Context ctx;
	init_builtins(ctx);

	auto body = ExpressionSeq{{
		make_expr<TryExpr>(
			std::vector{
				make_expr<TryExpr>(
					std::vector{
						make_expr<LetExpr>("x", make_expr<IntExpr>(2)),
						make_expr<ThrowExpr>(make_expr<VariableExpr>("x")),
						make_expr<AssignExpr>("x", make_expr<IntExpr>(0)),
						make_expr<ReturnExpr>(make_expr<VariableExpr>("x"))
					},
					"x",
					std::vector{
						make_expr<LetExpr>("y", make_expr<CallExpr>(
							make_expr<VariableExpr>("+"),
							std::vector{
								make_expr<VariableExpr>("x"),
								make_expr<IntExpr>(1)
							}
						)),
						make_expr<VariableExpr>("y")
					}
				)
			},
			"_",
			std::vector{
				make_expr<IntExpr>(0)
			}
		)
	}};

	auto compiler = ctx.root(Compiler(ctx));
	auto main = compiler->compile(body);
	auto vm = ctx.root(VM(ctx));
	auto result = vm->run(*main)->get<int64_t>();
	EXPECT_EQ(result, 3) << "Evaluation result is wrong";
}

TEST(CompilerTest, NestedBlocks) {
	// {
	//     let x = 2;
	//     let y = 10;
	//     let z = {
	//         let x = 5;
	//         {
	//             x = { y = y + 10; y };
	//         }
	//         x
	//     };
	//     x = x + y + z
	//     x
	// }

	Context ctx;
	init_builtins(ctx);

	auto body = ExpressionSeq{{
		make_expr<BlockExpr>(std::vector{
			make_expr<LetExpr>("x", make_expr<IntExpr>(2)),
			make_expr<LetExpr>("y", make_expr<IntExpr>(10)),
			make_expr<LetExpr>("z", make_expr<BlockExpr>(std::vector{
				make_expr<LetExpr>("x", make_expr<IntExpr>(5)),
				make_expr<BlockExpr>(std::vector{
					make_expr<AssignExpr>("x", make_expr<BlockExpr>(std::vector{
						make_expr<AssignExpr>("y", make_expr<CallExpr>(
							make_expr<VariableExpr>("+"),
							std::vector{
								make_expr<VariableExpr>("y"),
								make_expr<IntExpr>(10)
							}
						)),
						make_expr<VariableExpr>("y")
					}))
				}),
				make_expr<VariableExpr>("x")
			})),
			make_expr<AssignExpr>("x", make_expr<CallExpr>(
				make_expr<VariableExpr>("+"),
				std::vector{
					make_expr<VariableExpr>("x"),
					make_expr<CallExpr>(
						make_expr<VariableExpr>("+"),
						std::vector{
							make_expr<VariableExpr>("y"),
							make_expr<VariableExpr>("z")
						}
					)
				}
			)),
			make_expr<VariableExpr>("x")
		})
	}};

	auto compiler = ctx.root(Compiler(ctx));
	auto main = compiler->compile(body);
	auto vm = ctx.root(VM(ctx));
	auto result = vm->run(*main)->get<int64_t>();
	EXPECT_EQ(result, 42) << "Evaluation result is wrong";
}

TEST(CompilerTest, FibIter) {
	// fn fib(n) {
	//     let x = 0;
	//     let y = 1;
	//     while n != 0 {
	//         let z = x + y;
	//         x = y;
	//         y = z;
	//         n = n - 1;
	//     }
	//     x
	// }
	//
	// fib(5)

	Context ctx;
	init_builtins(ctx);

	auto body = ExpressionSeq{{
		make_expr<LetExpr>("fib", make_expr<LambdaExpr>(
			std::vector<std::string>{"n"},
			std::vector{
				make_expr<LetExpr>("x", make_expr<IntExpr>(0)),
				make_expr<LetExpr>("y", make_expr<IntExpr>(1)),
				make_expr<WhileExpr>(
					make_expr<CallExpr>(
						make_expr<VariableExpr>("!="),
						std::vector{
							make_expr<VariableExpr>("n"),
							make_expr<IntExpr>(0)
						}
					),
					std::vector{
						make_expr<LetExpr>("z", make_expr<CallExpr>(
							make_expr<VariableExpr>("+"),
							std::vector{
								make_expr<VariableExpr>("x"),
								make_expr<VariableExpr>("y")
							}
						)),
						make_expr<AssignExpr>("x", make_expr<VariableExpr>("y")),
						make_expr<AssignExpr>("y", make_expr<VariableExpr>("z")),
						make_expr<AssignExpr>("n", make_expr<CallExpr>(
							make_expr<VariableExpr>("-"),
							std::vector{
								make_expr<VariableExpr>("n"),
								make_expr<IntExpr>(1)
							}
						))
					}
				),
				make_expr<VariableExpr>("x")
			}
		)),
		make_expr<CallExpr>(
			make_expr<VariableExpr>("fib"),
			std::vector{make_expr<IntExpr>(5)}
		)
	}};

	std::vector<std::pair<int64_t, int64_t>> inputs{
		{0, 0},
		{1, 1},
		{4, 3},
		{7, 13},
		{10, 55},
		{15, 610}
	};
	auto compiler = ctx.root(Compiler(ctx));
	auto vm = ctx.root(VM(ctx));
	for (auto& pair : inputs) {
		body.exprs[1]->get<CallExpr>().args[0]->get<IntExpr>().value = pair.first;
		auto main = compiler->compile(body);
		auto actual = vm->run(*main)->get<int64_t>();
		auto expected = pair.second;
		EXPECT_EQ(actual, expected) <<
			"fib(" << pair.first << ") result is wrong";
	}
}

TEST(CompilerTest, BreakContinue) {
	// let x = 0;
	// let i = 0;
	// while true {
	//     let j = 0;
	//     while true {
	//         if j == 3 {
	//             break
	//         }
	//         j = j + 1;
	//         x = x + 1;
	//     }
	//     if i == 10 {
	//         break;
	//     }
	//     else {
	//	       i = i + 1;
	//         continue;
	//     };
	//     return 0;
	// };
	// return x;

	Context ctx;
	init_builtins(ctx);

	auto body = ExpressionSeq{{
		make_expr<LetExpr>("x", make_expr<IntExpr>(0)),
		make_expr<LetExpr>("i", make_expr<IntExpr>(0)),
		make_expr<WhileExpr>(make_expr<VariableExpr>("true"), std::vector{

			make_expr<LetExpr>("j", make_expr<IntExpr>(0)),
			make_expr<WhileExpr>(make_expr<VariableExpr>("true"), std::vector{
				make_expr<IfExpr>(
					std::vector{std::pair{
						make_expr<CallExpr>(
							make_expr<VariableExpr>("=="),
							std::vector{
								make_expr<VariableExpr>("j"),
								make_expr<IntExpr>(3)
							}
						),
						std::vector{make_expr<BreakExpr>()}
					}},
					std::nullopt
				),
				make_expr<AssignExpr>("j", make_expr<CallExpr>(
					make_expr<VariableExpr>("+"),
					std::vector{make_expr<VariableExpr>("j"), make_expr<IntExpr>(1)}
				)),
				make_expr<AssignExpr>("x", make_expr<CallExpr>(
					make_expr<VariableExpr>("+"),
					std::vector{make_expr<VariableExpr>("x"), make_expr<IntExpr>(1)}
				))
			}),

			make_expr<IfExpr>(
				std::vector{std::pair{
					make_expr<CallExpr>(
						make_expr<VariableExpr>("=="),
						std::vector{
							make_expr<VariableExpr>("i"),
							make_expr<IntExpr>(10)
						}
					),
					std::vector{
						make_expr<BreakExpr>()
					}
				}},
				std::optional{std::vector{
					make_expr<AssignExpr>("i", make_expr<CallExpr>(
						make_expr<VariableExpr>("+"),
						std::vector{
							make_expr<VariableExpr>("i"),
							make_expr<IntExpr>(1)
						}
					)),
					make_expr<ContinueExpr>()
				}}
			),

			make_expr<ReturnExpr>(make_expr<IntExpr>(0))
		}),
		make_expr<ReturnExpr>(make_expr<VariableExpr>("x"))
	}};

	auto compiler = ctx.root(Compiler(ctx));
	auto main = compiler->compile(body);
	auto vm = ctx.root(VM(ctx));
	auto result = vm->run(*main)->get<int64_t>();
	EXPECT_EQ(result, 33) << "Evaluation result is wrong";
}

TEST(CompilerTest, ClosureCounter) {
	// let init = 0;
	// let inc = 1;
	// fn main() {
	//     fn make() {
	//         let n = init;
	//         fn() {
	//             n = n + inc;
	//             return n;
	//         }
	//     };
	//     let counter = make();
	//     counter();
	//     counter();
	//     counter()
	// }
	//
	// main()

	Context ctx;
	init_builtins(ctx);

	auto body = ExpressionSeq{{
		make_expr<LetExpr>("init", make_expr<IntExpr>(0)),
		make_expr<LetExpr>("inc", make_expr<IntExpr>(1)),

		make_expr<LetExpr>("main", make_expr<LambdaExpr>(
			std::vector<std::string>{},
			std::vector{
				make_expr<LetExpr>("make", make_expr<LambdaExpr>(
					std::vector<std::string>{},
					std::vector{
						make_expr<LetExpr>("x", make_expr<VariableExpr>("init")),
						make_expr<LambdaExpr>(
							std::vector<std::string>{},
							std::vector{
								make_expr<AssignExpr>("x", make_expr<CallExpr>(
									make_expr<VariableExpr>("+"),
									std::vector{
										make_expr<VariableExpr>("x"),
										make_expr<VariableExpr>("inc")
									}
								)),
								make_expr<ReturnExpr>(make_expr<VariableExpr>("x"))
							}
						)
					}
				)),
				make_expr<LetExpr>("counter", make_expr<CallExpr>(
					make_expr<VariableExpr>("make"),
					std::vector<ExpressionPtr>{}
				)),
				make_expr<CallExpr>(
					make_expr<VariableExpr>("counter"),
					std::vector<ExpressionPtr>{}
				),
				make_expr<CallExpr>(
					make_expr<VariableExpr>("counter"),
					std::vector<ExpressionPtr>{}
				),
				make_expr<CallExpr>(
					make_expr<VariableExpr>("counter"),
					std::vector<ExpressionPtr>{}
				)
			}
		)),

		make_expr<CallExpr>(
			make_expr<VariableExpr>("main"),
			std::vector<ExpressionPtr>{}
		)
	}};

	auto compiler = ctx.root(Compiler(ctx));
	auto main = compiler->compile(body);
	auto vm = ctx.root(VM(ctx));
	auto result = vm->run(*main)->get<int64_t>();
	EXPECT_EQ(result, 3) << "Evaluation result is wrong";
}
