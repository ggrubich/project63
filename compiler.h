#pragma once

#include "gc.h"
#include "parser.h"
#include "value.h"

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace detail {

struct LoopEnv {
	// Addresses of jumps associated with each loop control instruction.
	std::vector<size_t> continue_jumps;
	std::vector<size_t> break_jumps;
};

struct BlockEnv {
	// Index of the first local used by the block.
	size_t bottom;
	// Local variables and their stack indices.
	// Definitions contain the currently accessible variables.
	// Declaration are predeclared variables intended for future use.
	// Allowing multiple declarations for the same variable is necessary
	// for implementing variable shadowing.
	std::unordered_map<std::string, size_t> definitions;
	std::unordered_map<std::string, std::deque<size_t>> declarations;
	// Contains an env only if the block is a loop.
	std::optional<LoopEnv> loop;
};

struct FunctionEnv {
	FunctionProto proto;
	// Number of values on function's data stack.
	size_t nlocals;
	// Lexical blocks, from outermost to innermost.
	std::vector<BlockEnv> blocks;
	// Available upvalues and their indices.
	std::unordered_map<std::string, size_t> upvalues;
};

}  // namespace detail

class Compiler {
private:
	Context& ctx;
	// Stack of functions being currently compiled, from outermost to innermost.
	std::vector<detail::FunctionEnv> functions;

public:
	Compiler(Context& ctx);

	void trace(Tracer& t) const;

	// Compiles a sequence of expressions into an executable function.
	Root<Ptr<Function>> compile(const ExpressionSeq& body);

private:
	void push_func();
	void pop_func();
	detail::FunctionEnv& peek_func();
	FunctionProto& peek_proto();
	void push_block();
	void pop_block();
	detail::BlockEnv& peek_block();
	void push_local();
	void pop_local();
	size_t get_address();

	void compile_instr(Opcode op);
	void compile_instr(Opcode op, uint32_t arg);

	void compile_pop(size_t n);
	void compile_nip(size_t n);
	void compile_pop_all();
	void compile_nip_all();

	void compile_constant(const Value& value);
	void compile_string(const std::string& str);
	void compile_int(int64_t n);
	void compile_nil();

	void compile_variable(const VariableExpr& expr);
	void compile_let(const LetExpr& expr);
	void compile_assign(const AssignExpr& expr);
	std::optional<size_t> lookup_local(const std::string& name);
	std::optional<size_t> lookup_upvalue(const std::string& name);
	std::optional<size_t> lookup_upvalue_rec(size_t index, const std::string& name);
	std::optional<size_t> lookup_upvalue_origin(size_t index, const std::string& name);

	void compile_get_prop(const GetPropExpr& expr);
	void compile_set_prop(const SetPropExpr& expr);

	void compile_call(const CallExpr& expr);
	void compile_send(const SendExpr& expr);
	void compile_unary(const UnaryExpr& expr);
	void compile_binary(const BinaryExpr& expr);

	void declare_expr(const Expression& expr);
	void define_variable(const std::string& name);

	void compile_block(const std::vector<ExpressionPtr>& exprs);
	void compile_if(const IfExpr& expr);
	void compile_while(const WhileExpr& expr);
	void compile_try(const TryExpr& expr);

	void compile_lambda(const LambdaExpr& expr);
	void compile_method(const MethodExpr& expr);

	void compile_break(const BreakExpr&);
	void compile_continue(const ContinueExpr&);
	void compile_return(const ReturnExpr& expr);
	void compile_throw(const ThrowExpr& expr);

	void compile_expr(const Expression& expr);
	void compile_expr_chain(const std::vector<ExpressionPtr>& exprs);

	Root<Ptr<Function>> compile_main(const std::vector<ExpressionPtr>& body);
};

template<> struct Trace<Compiler> {
	void operator()(const Compiler& x, Tracer& t) {
		x.trace(t);
	}
};
