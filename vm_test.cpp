#include "vm.h"

#include <cassert>
#include <tuple>

#include <gtest/gtest.h>

namespace {

template<typename F>
Root<Ptr<CppFunction>> make_unary(Context& ctx, F f) {
	return ctx.alloc(CppLambda(1, [=](Context& ctx, const std::vector<Value>& xs) {
		assert(xs.size() == 1);
		auto x = std::get<int64_t>(xs[0].inner);
		return ctx.root(Value(f(x)));
	}));
}

template<typename F>
Root<Ptr<CppFunction>> make_binary(Context& ctx, F f) {
	return ctx.alloc(CppLambda(2, [=](Context& ctx, const std::vector<Value>& xs) {
		assert(xs.size() == 2);
		auto x = std::get<int64_t>(xs[0].inner);
		auto y = std::get<int64_t>(xs[1].inner);
		return ctx.root(Value(f(x, y)));
	}));
}

}  // namespace anonymous

TEST(VmTest, Factorial) {
	Context ctx;

	auto positive = make_unary(ctx, [](int64_t x) { return x > 0; });
	auto pred = make_unary(ctx, [](int64_t x) { return x - 1; });
	auto mult = make_binary(ctx, std::multiplies<int64_t>{});

	auto fact = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*fact)->proto->nargs = 1;
	(*fact)->proto->code = std::vector<Instruction>{
		// Var(0) = iterator
		// Var(1) = accumulator
		Instruction(Opcode::GetConst, 0),

		// return if iterator is non-positive, otherwise enter the loop
		Instruction(Opcode::GetConst, 2),  // positive
		Instruction(Opcode::GetVar, 0),  // iterator
		Instruction(Opcode::GetConst, 0),  // 1
		Instruction(Opcode::Call),
		Instruction(Opcode::JumpIf, 8),
		Instruction(Opcode::GetVar, 1),
		Instruction(Opcode::Return),

		// multiply thea accumulator
		Instruction(Opcode::GetConst, 4),  // mult
		Instruction(Opcode::GetVar, 0),
		Instruction(Opcode::GetVar, 1),
		Instruction(Opcode::GetConst, 1),  // 2
		Instruction(Opcode::Call),
		Instruction(Opcode::SetVar, 1),
		// decrement the iterator
		Instruction(Opcode::GetConst, 3),  // pred
		Instruction(Opcode::GetVar, 0),
		Instruction(Opcode::GetConst, 0),  // 1
		Instruction(Opcode::Call),
		Instruction(Opcode::SetVar, 0),

		// jump to start of the loop
		Instruction(Opcode::Jump, 1),
	};
	(*fact)->proto->constants = std::vector<Value>{
		1,
		2,
		*positive,
		*pred,
		*mult
	};

	auto main = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*main)->proto->nargs = 0;
	(*main)->proto->code = std::vector<Instruction>{
		Instruction(Opcode::GetUp, 0),  // fact
		Instruction(Opcode::GetConst, 1),  // n
		Instruction(Opcode::GetConst, 0),  // 1
		Instruction(Opcode::Call),
		Instruction(Opcode::Return)
	};
	(*main)->proto->constants = std::vector<Value>{
		Value(1),
		Value(0),  // n
	};
	(*main)->upvalues = std::vector<Ptr<Upvalue>>{
		*ctx.alloc<Upvalue>(Value(*fact))
	};

	std::vector<std::pair<int64_t, int64_t>> inputs = {
		{0, 1},
		{1, 1},
		{2, 2},
		{7, 5040},
		{10, 3628800}
	};
	auto vm = ctx.root(VM(ctx));
	for (auto p : inputs) {
		(*main)->proto->constants[1] = p.first;
		auto actual = vm->call(*main, {})->get<int64_t>();
		auto expected = p.second;
		EXPECT_EQ(actual, expected) << "fact(" << p.first << ") yields wrong result";
	}
}

TEST(VmTest, Fibbonacci) {
	Context ctx;

	auto less = make_binary(ctx, std::less<int64_t>{});
	auto sub = make_binary(ctx, std::minus<int64_t>{});
	auto add = make_binary(ctx, std::plus<int64_t>{});

	auto fib = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*fib)->proto->nargs = 1;
	(*fib)->proto->code = std::vector<Instruction>{
		// return n if n < 2
		Instruction(Opcode::GetConst, 2),  // less
		Instruction(Opcode::GetVar, 0),  // n
		Instruction(Opcode::GetConst, 1),  // 2
		Instruction(Opcode::GetConst, 1),  // 2
		Instruction(Opcode::Call),
		Instruction(Opcode::JumpUnless, 7),
		Instruction(Opcode::Return),

		// fib(n-1) + fib(n-2)
		Instruction(Opcode::GetConst, 4),  // add

		Instruction(Opcode::GetConst, 5),  // fib
		Instruction(Opcode::GetConst, 3),  // sub
		Instruction(Opcode::GetVar, 0),  // n
		Instruction(Opcode::GetConst, 0),  // 1
		Instruction(Opcode::GetConst, 1),  // 2
		Instruction(Opcode::Call),
		Instruction(Opcode::GetConst, 0),  // 1
		Instruction(Opcode::Call),

		Instruction(Opcode::GetConst, 5),  // fib
		Instruction(Opcode::GetConst, 3),  // sub
		Instruction(Opcode::GetVar, 0),  // n
		Instruction(Opcode::GetConst, 1),  // 2
		Instruction(Opcode::GetConst, 1),  // 2
		Instruction(Opcode::Call),
		Instruction(Opcode::GetConst, 0),  // 1
		Instruction(Opcode::Call),

		Instruction(Opcode::GetConst, 1),  // 2
		Instruction(Opcode::Call),
		Instruction(Opcode::Return)
	};
	(*fib)->proto->constants = std::vector<Value>{
		Value(1),
		Value(2),
		Value(*less),
		Value(*sub),
		Value(*add),
		Value(*fib)
	};

	auto main = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*main)->proto->nargs = 0;
	(*main)->proto->code = std::vector<Instruction>{
		Instruction(Opcode::GetConst, 2),  // fib
		Instruction(Opcode::GetConst, 1),  // n
		Instruction(Opcode::GetConst, 0),  // 1
		Instruction(Opcode::Call),
		Instruction(Opcode::Return)
	};
	(*main)->proto->constants = std::vector<Value>{
		Value(1),
		Value(1),  // n
		Value(*fib),
	};

	auto vm = ctx.root(VM(ctx));
	std::vector<std::pair<int64_t, int64_t>> inputs{
		{0, 0},
		{1, 1},
		{2, 1},
		{3, 2},
		{4, 3},
		{7, 13},
		{10, 55}
	};
	for (auto p : inputs) {
		(*main)->proto->constants[1] = Value(p.first);
		auto actual = vm->call(*main, {})->get<int64_t>();
		auto expected = p.second;
		EXPECT_EQ(actual, expected) << "fib(" << p.first << ") yields wrong result";
	}
}

TEST(VmTest, Closures) {
	Context ctx;

	auto add = make_binary(ctx, std::plus<int64_t>{});

	// Generates the next number.
	// Excpects 2 upvalues (increment and accumulator).
	auto next = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*next)->proto->nargs = 0;
	(*next)->proto->code = std::vector<Instruction>{
		Instruction(Opcode::GetConst, 1),  // add
		Instruction(Opcode::GetUp, 0),  // increment
		Instruction(Opcode::GetUp, 1),  // accumulator
		Instruction(Opcode::GetConst, 0),  // 2
		Instruction(Opcode::Call),
		Instruction(Opcode::SetUp, 1),
		Instruction(Opcode::GetUp, 1),
		Instruction(Opcode::Return)
	};
	(*next)->proto->constants = std::vector<Value>{
		Value(2),
		Value(*add),
	};

	// Creates the generator closure.
	// Expects 1 upvalue (increment)
	auto make = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*make)->proto->nargs = 0;
	(*make)->proto->code = std::vector<Instruction>{
		Instruction(Opcode::GetConst, 0),  // accumulator variable
		Instruction(Opcode::GetConst, 1),  // next proto
		Instruction(Opcode::ResetUp),
		Instruction(Opcode::CopyUp, 0),  // increment upvalue
		Instruction(Opcode::MakeUp, 0),  // accumulator upvalue
		Instruction(Opcode::Return)
	};
	(*make)->proto->constants = std::vector<Value>{
		Value(0),  // initial accumulator
		Value(*next)
	};

	auto main = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*main)->proto->nargs = 0;
	(*main)->proto->code = std::vector<Instruction>{
		Instruction(Opcode::GetConst, 0),  // increment variable
		Instruction(Opcode::GetConst, 2),  // make proto
		Instruction(Opcode::ResetUp),
		Instruction(Opcode::MakeUp, 0),  // increment upvalue
		Instruction(Opcode::GetConst, 1),  // 0
		Instruction(Opcode::Call),

		Instruction(Opcode::Dup),  // next
		Instruction(Opcode::GetConst, 1),  // 0
		Instruction(Opcode::Call),
		Instruction(Opcode::Pop),

		Instruction(Opcode::Dup),  // next
		Instruction(Opcode::GetConst, 1),  // 0
		Instruction(Opcode::Call),

		Instruction(Opcode::Return)
	};
	(*main)->proto->constants = std::vector<Value>{
		Value(3),  // increment
		Value(0),
		Value(*make)
	};

	auto vm = ctx.root(VM(ctx));
	auto actual = vm->call(*main, {})->get<int64_t>();
	EXPECT_EQ(actual, 6);
}

TEST(VmTest, Exceptions) {
	Context ctx;

	auto succ = make_unary(ctx, [](int64_t x) { return x + 1; });

	auto fail = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*fail)->proto->nargs = 1;
	(*fail)->proto->code = std::vector<Instruction>{
		Instruction(Opcode::Throw)
	};

	auto main = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*main)->proto->nargs = 0;
	(*main)->proto->code = std::vector<Instruction>{
		// Junk in variable 0.
		Instruction(Opcode::GetConst, 0),

		// Set up handlers.
		Instruction(Opcode::Catch, 100),  // jumps out of range
		Instruction(Opcode::Catch, 12),  // jumps to increment step

		// Push some junk onto the stack, then throw 3.
		Instruction(Opcode::GetConst, 0),  // 0
		Instruction(Opcode::Dup),
		Instruction(Opcode::GetConst, 1),
		Instruction(Opcode::GetConst, 3),  // fail
		Instruction(Opcode::GetConst, 2),  // 3
		Instruction(Opcode::GetConst, 1),  // 1
		Instruction(Opcode::Call),
		// Shouldn't be reachable
		Instruction(Opcode::GetConst, 0),
		Instruction(Opcode::Return),

		// Increment 3 (exception handler).
		Instruction(Opcode::GetConst, 4),  // succ
		Instruction(Opcode::GetVar, 1),  // 3
		Instruction(Opcode::GetConst, 1),  // 1
		Instruction(Opcode::Call),
		Instruction(Opcode::SetVar, 1),

		// Remove spurious exception handler.
		Instruction(Opcode::Uncatch),

		// Throw the incremented variable.
		Instruction(Opcode::GetConst, 3),  // fail
		Instruction(Opcode::GetVar, 1),  // 4
		Instruction(Opcode::GetConst, 1),  // 1
		Instruction(Opcode::Call)
	};
	(*main)->proto->constants = std::vector<Value>{
		0,
		1,
		3,
		*fail,
		*succ
	};

	auto vm = ctx.root(VM(ctx));
	try {
		vm->call(*main, {});
		EXPECT_FALSE(true) << "Main didn't throw";
	}
	catch (const Root<Value>& err) {
		auto actual = err->get<int64_t>();
		EXPECT_EQ(actual, 4) << "Value thrown from main is not 4";
	}
}

TEST(VmTest, Properties) {
	Context ctx;
	auto obj = ctx.alloc<Object>(ctx.object_cls);
	auto main = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*main)->proto->nargs = 0;
	(*main)->proto->code = std::vector<Instruction>{
		Instruction(Opcode::GetConst, 0),
		// @foo = "foo"
		Instruction(Opcode::GetVar, 0),
		Instruction(Opcode::GetConst, 1),
		Instruction(Opcode::GetConst, 1),
		Instruction(Opcode::SetProp),
		// @bar = "bar"
		Instruction(Opcode::GetVar, 0),
		Instruction(Opcode::GetConst, 2),
		Instruction(Opcode::GetConst, 2),
		Instruction(Opcode::SetProp),
		// @foo = @bar
		Instruction(Opcode::GetVar, 0),
		Instruction(Opcode::GetConst, 1),
		Instruction(Opcode::GetVar, 0),
		Instruction(Opcode::GetConst, 2),
		Instruction(Opcode::GetProp),
		Instruction(Opcode::SetProp),
		// return @foo
		Instruction(Opcode::GetVar, 0),
		Instruction(Opcode::GetConst, 1),
		Instruction(Opcode::GetProp),
		Instruction(Opcode::Return)
	};
	(*main)->proto->constants = std::vector<Value>{
		*obj,
		*ctx.alloc<std::string>("foo"),
		*ctx.alloc<std::string>("bar")
	};
	auto vm = ctx.root(VM(ctx));
	auto actual = *vm->call(*main, {})->get<Ptr<std::string>>();
	EXPECT_EQ(actual, "bar") << "Result of main is wrong";
}

TEST(VmTest, Methods) {
	Context ctx;

	// Base inherits from Object and defines following methods:
	//  - foo - returns "base_foo"
	auto base_cls = ctx.alloc<Klass>(ctx, ctx.object_cls);
	auto base_foo = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*base_foo)->proto->nargs = 1;
	(*base_foo)->proto->code = std::vector<Instruction>{
		Instruction(Opcode::GetConst, 0),
		Instruction(Opcode::Return)
	};
	(*base_foo)->proto->constants = std::vector<Value>{
		*ctx.alloc<std::string>("base_foo")
	};
	(*base_cls)->define(ctx, "foo", *base_foo);

	// Left inherits from Base and defines following methods:
	//  - foo - returns "derived_foo"
	auto left_cls = ctx.alloc<Klass>(ctx, *base_cls);
	auto left_foo = ctx.alloc(CppLambda(1, [](Context& ctx, const std::vector<Value>&) {
		return ctx.alloc<std::string>("derived_foo");
	}));
	(*left_cls)->define(ctx, "foo", *left_foo);

	// Right inherits from Base and defines following methods:
	//  - not_understood(msg) - returns "generated_" concatedated with msg
	auto right_cls = ctx.alloc<Klass>(ctx, *base_cls);
	auto right_not_understood = ctx.alloc(CppMethod(
		1,
		[](Context& ctx, const Value&, const std::vector<Value>& xs) {
			auto x = xs[0].get<Ptr<std::string>>();
			return ctx.alloc<std::string>(std::string("generated_") + *x);
		}
	));
	(*right_cls)->define(ctx, "not_understood", *right_not_understood);

	auto left = ctx.alloc<Object>(*left_cls);
	auto right = ctx.alloc<Object>(*right_cls);

	// Main sends message from const slot 1 to object from const
	// slot 0 and returns the result.
	auto main = ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	(*main)->proto->nargs = 0;
	(*main)->proto->code = std::vector<Instruction>{
		Instruction(Opcode::GetConst, 0),
		Instruction(Opcode::GetConst, 1),
		Instruction(Opcode::Send),
		Instruction(Opcode::Return)
	};
	(*main)->proto->constants = std::vector<Value>(2);

	// Test cases with the receiver, message and the expected result.
	// Null result means that we expect a throw.
	std::vector<std::tuple<Value, std::string, std::optional<std::string>>> inputs{
		{*left, "foo", std::string("derived_foo")},
		{*right, "foo", std::string("base_foo")},
		{*left, "bar", std::nullopt},
		{*right, "bar", std::string("generated_bar")}
	};
	auto vm = ctx.root(VM(ctx));
	for (const auto& input : inputs) {
		Value obj;
		std::string msg;
		std::optional<std::string> expected;
		std::tie(obj, msg, expected) = input;
		(*main)->proto->constants[0] = obj;
		(*main)->proto->constants[1] = *ctx.alloc<std::string>(msg);
		if (expected) {
			auto actual = *vm->call(*main, {})->get<Ptr<std::string>>();
			EXPECT_EQ(actual, *expected) << msg << "result is wrong";
		}
		else {
			EXPECT_THROW({ vm->call(*main, {}); }, Root<Value>)
				<< "Sending " << msg << " didn't throw an exception";
		}
	}
}
