#include "compiler.h"

#include <algorithm>
#include <cassert>
#include <sstream>

namespace detail {

BlockKind::BlockKind() : Variant(PlainBlock()) {}

}  // namespace detail

Compiler::Compiler(Context& ctx)
	: ctx(ctx)
	, functions()
{}

void Compiler::trace(Tracer& t) const {
	for (auto& func : functions) {
		Trace<FunctionProto>{}(func.proto, t);
	}
}

Root<Ptr<Function>> Compiler::compile(const ExpressionSeq& body) {
	return compile_main(body.exprs);
}

void Compiler::push_func() {
	functions.push_back(detail::FunctionEnv{});
}

void Compiler::pop_func() {
	functions.pop_back();
}

detail::FunctionEnv& Compiler::peek_func() {
	return functions.back();
}

FunctionProto& Compiler::peek_proto() {
	return peek_func().proto;
}

void Compiler::push_block() {
	auto& func = peek_func();
	detail::BlockEnv block;
	block.bottom = func.locals;
	block.kind = detail::PlainBlock{};
	func.blocks.push_back(block);
}

void Compiler::pop_block() {
	auto& func = peek_func();
	func.locals = func.blocks.back().bottom;
	func.blocks.pop_back();
}

detail::BlockEnv& Compiler::peek_block() {
	return peek_func().blocks.back();
}

void Compiler::push_local() {
	functions.back().locals += 1;
}

void Compiler::pop_local() {
	functions.back().locals -= 1;
}

size_t Compiler::get_address() {
	return peek_proto().code.size();
}

void Compiler::compile_instr(Opcode op) {
	peek_proto().code.push_back(Instruction(op));
}

void Compiler::compile_instr(Opcode op, uint32_t arg) {
	peek_proto().code.push_back(Instruction(op, arg));
}

void Compiler::compile_pop() {
	compile_instr(Opcode::Pop);
	pop_local();
}

void Compiler::compile_nip() {
	compile_instr(Opcode::Nip);
	pop_local();
}

void Compiler::compile_constant(const Value& value) {
	auto& proto = peek_proto();
	compile_instr(Opcode::GetConst, proto.constants.size());
	proto.constants.push_back(value);
	push_local();
}

void Compiler::compile_string(const std::string& str) {
	compile_constant(*ctx.alloc(str));
}

void Compiler::compile_int(int64_t n) {
	compile_constant(n);
}

void Compiler::compile_nil() {
	compile_instr(Opcode::Nil);
	push_local();
}

void Compiler::compile_variable(const VariableExpr& expr) {
	if (auto local = lookup_local(expr.name)) {
		compile_instr(Opcode::GetVar, *local);
	}
	else if (auto upvalue = lookup_upvalue(expr.name)) {
		compile_instr(Opcode::GetUp, *upvalue);
	}
	else {
		std::stringstream buf;
		buf << "Variable `" << expr.name << "` not found";
		throw std::runtime_error(buf.str());
	}
	push_local();
}

void Compiler::compile_let(const LetExpr& expr) {
	compile_expr(*expr.value);
	auto& name = expr.name;
	auto& block = peek_block();
	assert(block.declarations[name].size() > 0 &&
		"Variable was not predeclared");
	auto idx = block.declarations[name].front();
	block.declarations[name].pop_front();
	block.definitions[name] = idx;
	compile_instr(Opcode::Dup);
	compile_instr(Opcode::SetVar, idx);
}

void Compiler::compile_assign(const AssignExpr& expr) {
	compile_expr(*expr.value);
	compile_instr(Opcode::Dup);
	if (auto local = lookup_local(expr.name)) {
		compile_instr(Opcode::SetVar, *local);
	}
	else if (auto upvalue = lookup_upvalue(expr.name)) {
		compile_instr(Opcode::SetUp, *upvalue);
	}
	else {
		std::stringstream buf;
		buf << "Variable `" << expr.name << "` not found";
		throw std::runtime_error(buf.str());
	}
}

namespace {

template<typename K, typename V>
std::optional<V> find_opt(const std::unordered_map<K, V>& map, const K& key) {
	auto it = map.find(key);
	return it != map.end() ? std::optional(it->second) : std::nullopt;
}

}  // namespace anonymous

std::optional<size_t> Compiler::lookup_local(const std::string& name) {
	auto& func = peek_func();
	for (auto block = func.blocks.rbegin(); block != func.blocks.rend(); ++block) {
		if (auto idx = find_opt(block->definitions, name)) {
			return idx;
		}
	}
	return std::nullopt;
}

std::optional<size_t> Compiler::lookup_upvalue(const std::string& name) {
	return lookup_upvalue_rec(functions.size() - 1, name);
}

std::optional<size_t> Compiler::lookup_upvalue_rec(size_t level, const std::string& name) {
	auto& current = functions[level];
	// If no upvalue with the given name is present, try to create it.
	if (current.upvalues.count(name) == 0 && level > 0) {
		auto& outer = functions[level - 1];
		// Check if the enclosing closure has a local variable which can be captured.
		if (auto idx = lookup_upvalue_origin(level - 1, name)) {
			outer.proto.code.push_back(
				Instruction(Opcode::MakeUp, *idx)
			);
			current.upvalues.emplace(name, current.upvalues.size());
		}
		// Else, search for the upvalue recursively and copy it if found.
		else if (auto idx = lookup_upvalue_rec(level - 1, name)) {
			outer.proto.code.push_back(
				Instruction(Opcode::CopyUp, *idx)
			);
			current.upvalues.emplace(name, current.upvalues.size());
		}
	}
	return find_opt(current.upvalues, name);
}

std::optional<size_t> Compiler::lookup_upvalue_origin(size_t level, const std::string& name) {
	auto& func = functions[level];
	for (auto block = func.blocks.rbegin(); block != func.blocks.rend(); ++block) {
		if (auto idx = find_opt(block->definitions, name)) {
			return idx;
		}
		if (block->declarations.count(name) > 0) {
			auto& indices = block->declarations[name];
			if (indices.size() > 0) {
				return indices.front();
			}
		}
	}
	return std::nullopt;
}

void Compiler::compile_get_prop(const GetPropExpr& expr) {
	compile_expr(*expr.obj);
	compile_string(expr.name);
	compile_instr(Opcode::GetProp);
	pop_local();
}

void Compiler::compile_set_prop(const SetPropExpr& expr) {
	compile_expr(*expr.obj);
	compile_instr(Opcode::Dup);
	compile_string(expr.name);
	compile_expr(*expr.value);
	compile_instr(Opcode::SetProp);
	pop_local();
	pop_local();
}

void Compiler::compile_get_index(const GetIndexExpr& expr) {
	compile_call(CallExpr{
		make_expr<SendExpr>(expr.obj, "[]"),
		expr.keys
	});
}

void Compiler::compile_set_index(const SetIndexExpr& expr) {
	auto args = expr.keys;
	args.emplace_back(expr.value);
	compile_call(CallExpr{
		make_expr<SendExpr>(expr.obj, "[]="),
		args
	});
}

void Compiler::compile_call(const CallExpr& expr) {
	compile_expr(*expr.func);
	for (auto& arg : expr.args) {
		compile_expr(*arg);
	}
	compile_int(expr.args.size());
	compile_instr(Opcode::Call);
	for (size_t i = 0; i < expr.args.size(); ++i) {
		pop_local();
	}
	pop_local();
}

void Compiler::compile_send(const SendExpr& expr) {
	compile_expr(*expr.obj);
	compile_string(expr.msg);
	compile_instr(Opcode::Send);
	pop_local();
}

void Compiler::compile_unary(const UnaryExpr& expr) {
	compile_send(SendExpr{expr.value, expr.op});
}

void Compiler::compile_binary(const BinaryExpr& expr) {
	compile_call(CallExpr{
		make_expr<SendExpr>(expr.lhs, expr.op),
		std::vector{expr.rhs}
	});
}

void Compiler::declare_expr(const Expression& expr) {
	expr.visit(Overloaded {
		[](const StringExpr&) {},
		[](const IntExpr&) {},
		[](const EmptyExpr&) {},
		[](const VariableExpr&) {},
		[&](const LetExpr& expr) {
			declare_expr(*expr.value);
			peek_block().declarations[expr.name].push_back(
				peek_func().locals
			);
			compile_nil();
		},
		[&](const AssignExpr& expr) {
			declare_expr(*expr.value);
		},
		[&](const GetPropExpr& expr) {
			declare_expr(*expr.obj);
		},
		[&](const SetPropExpr& expr) {
			declare_expr(*expr.obj);
			declare_expr(*expr.value);
		},
		[&](const GetIndexExpr& expr) {
			declare_expr(*expr.obj);
			declare_expr_chain(expr.keys);
		},
		[&](const SetIndexExpr& expr) {
			declare_expr(*expr.obj);
			declare_expr_chain(expr.keys);
			declare_expr(*expr.value);
		},
		[&](const CallExpr& expr) {
			declare_expr(*expr.func);
			declare_expr_chain(expr.args);
		},
		[&](const SendExpr& expr) {
			declare_expr(*expr.obj);
		},
		[&](const UnaryExpr& expr) {
			declare_expr(*expr.value);
		},
		[&](const BinaryExpr& expr) {
			declare_expr(*expr.lhs);
			declare_expr(*expr.rhs);
		},
		[&](const AndExpr& expr) {
			declare_expr(*expr.lhs);
		},
		[&](const OrExpr& expr) {
			declare_expr(*expr.lhs);
		},
		[](const BlockExpr&) {},
		[](const IfExpr&) {},
		[](const WhileExpr&) {},
		[](const TryExpr&) {},
		[](const DeferExpr&) {},
		[](const LambdaExpr&) {},
		[](const MethodExpr&) {},
		[](const BreakExpr&) {},
		[](const ContinueExpr&) {},
		[&](const ReturnExpr& expr) {
			if (expr.value) {
				declare_expr(**expr.value);
			}
		},
		[&](const ThrowExpr& expr) {
			declare_expr(*expr.value);
		}
	});
}

void Compiler::declare_expr_chain(const std::vector<ExpressionPtr>& exprs) {
	for (auto& expr : exprs) {
		declare_expr(*expr);
	}
}

void Compiler::define_variable(const std::string& name) {
	peek_block().definitions[name] = peek_func().locals;
	push_local();
}

void Compiler::compile_leave(size_t nblocks) {
	auto& func = peek_func();
	for (auto block = func.blocks.rbegin();
			block != func.blocks.rbegin()+nblocks;
			++block)
	{
		for (auto defer = block->deferrals.rbegin();
				defer != block->deferrals.rend();
				++defer)
		{
			compile_instr(Opcode::Uncatch);
			// Adjust stack indices and jump addresses.
			size_t idx_diff = func.locals - defer->bottom;
			size_t addr_diff = get_address() - defer->address;
			for (auto instr : defer->code) {
				switch (instr.op) {
				case Opcode::GetVar:
				case Opcode::SetVar:
				case Opcode::MakeUp:
					instr.arg = size_t(instr.arg) + idx_diff;
					break;
				case Opcode::Jump:
				case Opcode::JumpIf:
				case Opcode::JumpUnless:
				case Opcode::Catch:
					instr.arg = size_t(instr.arg) + addr_diff;
					break;
				default:
					break;
				}
				compile_instr(instr.op, instr.arg);
			}
		}
		if (block->kind.holds<detail::TryBlock>()) {
			compile_instr(Opcode::Uncatch);
		}
	}
}

void Compiler::compile_leave_pop(size_t nblocks) {
	compile_leave(nblocks);
	if (nblocks > 0) {
		auto& func = peek_func();
		assert(func.locals >= (func.blocks.end()-nblocks)->bottom &&
			"Local stack underflow");
		size_t n = func.locals - (func.blocks.end()-nblocks)->bottom;
		while (n-- > 0) {
			compile_instr(Opcode::Pop);
		}
	}
}

void Compiler::compile_leave_nip(size_t nblocks) {
	compile_leave(nblocks);
	if (nblocks > 0) {
		auto& func = peek_func();
		assert(func.locals > (func.blocks.end()-nblocks)->bottom &&
			"Local stack underflow");
		size_t n = func.locals - (func.blocks.end()-nblocks)->bottom - 1;
		while (n-- > 0) {
			compile_instr(Opcode::Nip);
		}
	}
}

// For short-circuit operators, we evaluate the rhs in a nested block.
// This prevents variables declarated in it from being conditionally defined.

void Compiler::compile_and(const AndExpr& expr) {
	// eval lhs
	compile_expr(*expr.lhs);
	compile_instr(Opcode::Dup);
	size_t finish_jump = get_address();
	compile_instr(Opcode::JumpUnless, -1);
	// eval rhs
	compile_pop();
	push_block();
	declare_expr(*expr.rhs);
	compile_expr(*expr.rhs);
	compile_leave_nip();
	pop_block();
	push_local();
	// finish
	peek_proto().code[finish_jump].arg = get_address();
}

void Compiler::compile_or(const OrExpr& expr) {
	// eval lhs
	compile_expr(*expr.lhs);
	compile_instr(Opcode::Dup);
	size_t finish_jump = get_address();
	compile_instr(Opcode::JumpIf, -1);
	// eval rhs
	compile_pop();
	push_block();
	declare_expr(*expr.rhs);
	compile_expr(*expr.rhs);
	compile_leave_nip();
	pop_block();
	push_local();
	// finish
	peek_proto().code[finish_jump].arg = get_address();
}

void Compiler::compile_block(const std::vector<ExpressionPtr>& exprs) {
	if (exprs.size() == 0) {
		compile_nil();
		return;
	}
	push_block();
	declare_expr_chain(exprs);
	compile_expr_chain(exprs);
	compile_leave_nip();
	pop_block();
	push_local();
}

void Compiler::compile_if(const IfExpr& expr) {
	// We treat the if branches as two nested blocks - first contains the
	// predicate expression, second contains the branch body. This allows us
	// to correctly evaluate the predicate withous predeclaring variables for
	// the body.
	std::vector<size_t> finish_jumps;
	for (auto& branch : expr.branches) {
		// Evaluate the predicate.
		push_block();
		declare_expr(*branch.first);
		compile_expr(*branch.first);
		size_t next_jump = get_address();
		compile_instr(Opcode::JumpUnless, -1);
		pop_local();
		// If true, evaluate the body, pop locals and jump to the end.
		compile_block(branch.second);
		compile_leave_nip();
		finish_jumps.push_back(get_address());
		compile_instr(Opcode::Jump, -1);
		pop_local();
		// If false, pop block locals and go to the next branch.
		peek_proto().code[next_jump].arg = get_address();
		compile_leave_pop();
		// Clean up the locals before next iteration.
		pop_block();
	}
	if (expr.otherwise) {
		compile_block(*expr.otherwise);
	}
	else {
		compile_nil();
	}
	for (auto jump : finish_jumps) {
		peek_proto().code[jump].arg = get_address();
	}
}

void Compiler::compile_while(const WhileExpr& expr) {
	// Evaluate the condition in a fresh block. Target address of the jump
	// will be set later.
	size_t start_addr = get_address();
	push_block();
	declare_expr(*expr.cond);
	compile_expr(*expr.cond);
	size_t finish_jump = get_address();
	compile_instr(Opcode::JumpUnless, -1);
	pop_local();
	// If true, evaluate the loop body, pop the result and block variables
	// and jump to the start.
	peek_block().kind = detail::LoopBlock{};
	compile_block(expr.body);
	compile_pop();
	for (auto jump : peek_block().kind.get<detail::LoopBlock>().continue_jumps) {
		// (continues join here)
		peek_proto().code[jump].arg = get_address();
	}
	compile_leave_pop();
	compile_instr(Opcode::Jump, start_addr);
	// If false, pop block variables and push nil as a result.
	peek_proto().code[finish_jump].arg = get_address();
	for (auto jump : peek_block().kind.get<detail::LoopBlock>().break_jumps) {
		// (breaks join here)
		peek_proto().code[jump].arg = get_address();
	}
	compile_leave_pop();
	pop_block();
	compile_nil();
}

void Compiler::compile_try(const TryExpr& expr) {
	// Set up the handler, evaluate the body.
	size_t handler_jump = get_address();
	compile_instr(Opcode::Catch, -1);
	push_block();
	peek_block().kind = detail::TryBlock{};
	declare_expr_chain(expr.body);
	compile_expr_chain(expr.body);
	compile_leave_nip();
	pop_block();
	// If no errors were thrown, jump to the end.
	size_t finish_jump = get_address();
	compile_instr(Opcode::Jump, -1);
	// If error was thrown, bind it to a variable and run the handler.
	peek_proto().code[handler_jump].arg = get_address();
	push_block();
	define_variable(expr.error);
	compile_block(expr.handler);
	compile_nip();
	pop_block();
	// Finish
	peek_proto().code[finish_jump].arg = get_address();
	push_local();
}

void Compiler::compile_defer(const DeferExpr& expr) {
	detail::Deferral defer;
	// Push an exception handler which runs the defer contents and rethrows
	// the exception. Copy the hander code to a deferral object.
	compile_instr(Opcode::Catch, get_address()+2);
	size_t skip_jump = get_address();
	compile_instr(Opcode::Jump, -1);
	push_local();
	// Handler itself.
	defer.address = get_address();
	defer.bottom = peek_func().locals;
	push_block();
	peek_block().kind = detail::DeferBlock{};
	declare_expr(*expr.expr);
	compile_expr(*expr.expr);
	compile_leave_pop();
	pop_block();
	defer.code = std::vector<Instruction>(
		peek_proto().code.begin() + defer.address,
		peek_proto().code.end()
	);
	// Rethrow and finalize.
	compile_instr(Opcode::Throw);
	pop_local();
	peek_proto().code[skip_jump].arg = get_address();
	peek_block().deferrals.emplace_back(defer);
	compile_nil();
}

void Compiler::compile_lambda(const LambdaExpr& expr) {
	// Load the lambda in the outer function.
	compile_instr(Opcode::GetConst, peek_proto().constants.size());
	compile_instr(Opcode::ResetUp);
	push_local();
	// Compile the inner function.
	push_func();
	peek_proto().nargs = expr.args.size();
	push_block();
	for (auto& arg : expr.args) {
		define_variable(arg);
	}
	push_block();
	declare_expr_chain(expr.body);
	compile_expr_chain(expr.body);
	compile_leave();
	compile_instr(Opcode::Return);
	// Move the constructed value to the constant.
	auto value = ctx.alloc<Function>(*ctx.alloc(std::move(peek_proto())));
	pop_func();
	peek_proto().constants.push_back(*value);
}

void Compiler::compile_method(const MethodExpr& expr) {
	// Transpile the method to lambdas and reuse lambda compilation function.
	LambdaExpr lambda;
	if (expr.args) {
		lambda = LambdaExpr{
			std::vector<std::string>{"self"},
			std::vector{
				make_expr<LambdaExpr>(*expr.args, expr.body)
			}
		};
	}
	else {
		lambda = LambdaExpr{std::vector<std::string>{"self"}, expr.body};
	}
	compile_lambda(lambda);
}

// For the sake of the compilaion, we pretend that control flow altering
// expressions return some value. This doesn't matter at runtime, but it
// affects compiler's stack simulation. Ideally, we would just eliminate
// dead code altogether and not deal with it, but this solution should
// be okay for now.

void Compiler::compile_loop_control(bool cont) {
	auto& func = peek_func();
	auto loop_block = func.blocks.rend();
	for (auto block = func.blocks.rbegin(); ; ++block) {
		if (block == func.blocks.rend() ||
				block->kind.holds<detail::DeferBlock>())
		{
			throw std::runtime_error("Break and continue can only be used inside of a loop");
		}
		else if (block->kind.holds<detail::LoopBlock>()) {
			loop_block = block;
			break;
		}
	}
	size_t nblocks = loop_block - func.blocks.rbegin();
	compile_leave_pop(nblocks);
	auto& loop = loop_block->kind.get<detail::LoopBlock>();
	if (cont) {
		loop.continue_jumps.emplace_back(get_address());
	}
	else {
		loop.break_jumps.emplace_back(get_address());
	}
	compile_instr(Opcode::Jump, -1);
	push_local();
}

void Compiler::compile_break(const BreakExpr&) {
	compile_loop_control(false);
}

void Compiler::compile_continue(const ContinueExpr&) {
	compile_loop_control(true);
}

void Compiler::compile_return(const ReturnExpr& expr) {
	for (auto block : peek_func().blocks) {
		if (block.kind.holds<detail::DeferBlock>()) {
			throw std::runtime_error("Can't return from a defer");
		}
	}
	if (expr.value) {
		compile_expr(**expr.value);
	}
	else {
		compile_nil();
	}
	compile_leave(peek_func().blocks.size());
	compile_instr(Opcode::Return);
}

void Compiler::compile_throw(const ThrowExpr& expr) {
	compile_expr(*expr.value);
	compile_instr(Opcode::Throw);
}

void Compiler::compile_expr(const Expression& expr) {
	expr.visit(Overloaded {
		[&](const StringExpr& expr) {
			compile_string(expr.value);
		},
		[&](const IntExpr& expr) {
			compile_int(expr.value);
		},
		[&](const EmptyExpr&) {
			compile_nil();
		},
		[&](const VariableExpr& expr) {
			compile_variable(expr);
		},
		[&](const LetExpr& expr) {
			compile_let(expr);
		},
		[&](const AssignExpr& expr) {
			compile_assign(expr);
		},
		[&](const GetPropExpr& expr) {
			compile_get_prop(expr);
		},
		[&](const SetPropExpr& expr) {
			compile_set_prop(expr);
		},
		[&](const GetIndexExpr& expr) {
			compile_get_index(expr);
		},
		[&](const SetIndexExpr& expr) {
			compile_set_index(expr);
		},
		[&](const CallExpr& expr) {
			compile_call(expr);
		},
		[&](const SendExpr& expr) {
			compile_send(expr);
		},
		[&](const UnaryExpr& expr) {
			compile_unary(expr);
		},
		[&](const BinaryExpr& expr) {
			compile_binary(expr);
		},
		[&](const AndExpr& expr) {
			compile_and(expr);
		},
		[&](const OrExpr& expr) {
			compile_or(expr);
		},
		[&](const BlockExpr& expr) {
			compile_block(expr.exprs);
		},
		[&](const IfExpr& expr) {
			compile_if(expr);
		},
		[&](const WhileExpr& expr) {
			compile_while(expr);
		},
		[&](const TryExpr& expr) {
			compile_try(expr);
		},
		[&](const DeferExpr& expr) {
			compile_defer(expr);
		},
		[&](const LambdaExpr& expr) {
			compile_lambda(expr);
		},
		[&](const MethodExpr& expr) {
			compile_method(expr);
		},
		[&](const BreakExpr& expr) {
			compile_break(expr);
		},
		[&](const ContinueExpr& expr) {
			compile_continue(expr);
		},
		[&](const ReturnExpr& expr) {
			compile_return(expr);
		},
		[&](const ThrowExpr& expr) {
			compile_throw(expr);
		}
	});
}

void Compiler::compile_expr_chain(const std::vector<ExpressionPtr>& exprs) {
	if (exprs.size() == 0) {
		compile_nil();
		return;
	}
	auto it = exprs.begin();
	compile_expr(**it);
	for (++it; it != exprs.end(); ++it) {
		compile_pop();
		compile_expr(**it);
	}
}

Root<Ptr<Function>> Compiler::compile_main(const std::vector<ExpressionPtr>& body) {
	push_func();
	peek_proto().nargs = 0;
	push_block();
	for (auto& x : ctx.builtins) {
		compile_constant(x.second);
		pop_local();
		define_variable(x.first);
	}
	declare_expr_chain(body);
	compile_expr_chain(body);
	compile_leave();
	compile_instr(Opcode::Return);
	auto main = ctx.alloc<Function>(*ctx.alloc(std::move(peek_proto())));
	pop_func();
	return main;
}
