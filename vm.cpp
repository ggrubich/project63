#include "vm.h"

#include <cassert>

namespace detail {

DataFrame::DataFrame(const Value& value)
	: value(value)
	, upvalue(std::nullopt)
{}

}  // namespace detaul

VM::VM(Collector& gc) : gc(gc) {}

void VM::trace(Tracer& t) const {
	for (const auto& x : data_stack) {
		Traceable<Value>::trace(x.value, t);
		if (x.upvalue) {
			t.visit(*x.upvalue);
		}
	}
	for (const auto& x : call_stack) {
		t.visit(x.func);
	}
}

Root<Value> VM::run(const Value& main) {
	data_stack.clear();
	call_stack.clear();
	exception_stack.clear();
	exception_thrown = false;

	push_data(main);
	push_data(0);
	call();

	while (call_stack.size() > 0) {
		auto& frame = call_stack.back();
		const auto& code = frame.func->proto->code;
		assert(frame.ip < code.size() && "Instruction pointer out of range");
		auto instr = code[frame.ip++];
		switch (instr.op) {
		case Opcode::Nop:
			break;
		case Opcode::Pop:
			pop_data();
			break;
		case Opcode::Nip:
			nip_data();
			break;
		case Opcode::Dup:
			push_data(peek_data());
			break;

		case Opcode::GetVar:
			get_variable(instr.arg);
			break;
		case Opcode::SetVar:
			set_variable(instr.arg);
			break;

		case Opcode::GetConst:
			push_data(frame.func->proto->constants[instr.arg]);
			break;

		case Opcode::GetUp:
			get_upvalue(instr.arg);
			break;
		case Opcode::SetUp:
			set_upvalue(instr.arg);
			break;
		case Opcode::ResetUp:
			reset_upvalues();
			break;
		case Opcode::MakeUp:
			make_upvalue(instr.arg);
			break;
		case Opcode::CopyUp:
			copy_upvalue(instr.arg);
			break;

		case Opcode::Call:
			call();
			break;

		case Opcode::Return:
			return_();
			break;
		case Opcode::Jump:
			jump(instr.arg);
			break;
		case Opcode::JumpIf:
			jump_cond(instr.arg, true);
			break;
		case Opcode::JumpUnless:
			jump_cond(instr.arg, false);
			break;

		case Opcode::Throw:
			throw_();
			break;
		case Opcode::Catch:
			catch_(instr.arg);
			break;
		case Opcode::Uncatch:
			uncatch();
			break;
		}
	}
	assert(data_stack.size() == 1 && "Data stack final size mismatch");
	assert(call_stack.size() == 0 && "Call stack final size mismatch");
	assert(exception_stack.size() == 0 && "Call stack final size mismatch");
	auto result = gc.root(pop_data());
	if (exception_thrown) {
		throw result;
	} else {
		return result;
	}
}

Value VM::remove_data(size_t off) {
	assert(data_stack.size() > off && "Data stack underflow");
	size_t idx = data_stack.size() - 1 - off;
	auto value = data_stack[idx].value;
	if (data_stack[idx].upvalue) {
		**data_stack[idx].upvalue = value;
	}
	for (size_t i = idx; i < data_stack.size() - 1; ++i) {
		data_stack[i] = std::move(data_stack[i+1]);
		if (data_stack[i].upvalue) {
			auto& up = *data_stack[i].upvalue;
			up->get<uint64_t>() -= 1;
		}
	}
	data_stack.pop_back();
	return value;
}

Value VM::pop_data() {
	return remove_data(0);
}

void VM::nip_data() {
	remove_data(1);
}

Value& VM::get_data(size_t off) {
	assert(data_stack.size() > off && "Data stack underflow");
	size_t idx = data_stack.size() - 1 - off;
	return data_stack[idx].value;
}

Value& VM::peek_data() {
	return get_data(0);
}

void VM::push_data(const Value& value) {
	data_stack.emplace_back(value);
}

void VM::get_variable(size_t idx) {
	idx = call_stack.back().data_bottom + idx;
	assert(idx < data_stack.size() && "Variable out of range");
	push_data(data_stack[idx].value);
}

void VM::set_variable(size_t idx) {
	idx = call_stack.back().data_bottom + idx;
	assert(idx < data_stack.size() && "Variable out of range");
	data_stack[idx].value = pop_data();
}

void VM::get_upvalue(size_t idx) {
	auto& upvalues = call_stack.back().func->upvalues;
	assert(idx < upvalues.size() && "Upvalue out of range");
	push_data(upvalues[idx]->visit(Overloaded {
		[&](uint64_t i) { return data_stack[i].value; },
		[](Value v) { return v; }
	}));
}

void VM::set_upvalue(size_t idx) {
	auto& upvalues = call_stack.back().func->upvalues;
	assert(idx < upvalues.size() && "Upvalue out of range");
	auto value = pop_data();
	upvalues[idx]->visit(Overloaded {
		[&](uint64_t i) { data_stack[i].value = value; },
		[&](Value& v) { v = value; }
	});
}

void VM::reset_upvalues() {
	assert(peek_data().holds<Ptr<Function>>() &&
			"Accessing upvalues on non-function");
	auto func1 = gc.root(pop_data().get<Ptr<Function>>());
	auto func2 = gc.alloc<Function>((*func1)->proto);
	push_data(*func2);
}

void VM::make_upvalue(size_t idx) {
	assert(peek_data().holds<Ptr<Function>>() &&
			"Accessing upvalues on non-function");
	idx = call_stack.back().data_bottom + idx;
	assert(idx < data_stack.size() && "Variable out of range");
	if (!data_stack[idx].upvalue) {
		data_stack[idx].upvalue = *gc.alloc<Upvalue>(idx);
	}
	auto& func = peek_data().get<Ptr<Function>>();
	func->upvalues.push_back(*data_stack[idx].upvalue);
}

void VM::copy_upvalue(size_t idx) {
	assert(peek_data().holds<Ptr<Function>>() &&
			"Accessing upvalues on non-function");
	auto& func1 = call_stack.back().func;
	assert(idx < func1->upvalues.size() && "Upvalue ot of range");
	auto& func2 = peek_data().get<Ptr<Function>>();
	func2->upvalues.push_back(func1->upvalues[idx]);
}

void VM::call() {
	size_t n = pop_data().get<int64_t>();
	auto func = gc.root(remove_data(n));
	func->visit(Overloaded {
		[&](const Ptr<Function>& func) {
			call_native(func, n);
		},
		[&](const Ptr<CppFunction>& func) {
			call_foreign(func, n);
		},
		[&](const auto&) {
			throw_string("Can't call a non-function");
		}
	});
}

void VM::call_native(const Ptr<Function>& func, size_t n) {
	if (func->proto->nargs != n) {
		throw_string("Wrong number of arguments");
		return;
	}
	detail::CallFrame frame;
	frame.func = func;
	frame.ip = 0;
	frame.data_bottom = data_stack.size() - n;
	frame.exception_bottom = exception_stack.size();
	call_stack.push_back(frame);
}

void VM::call_foreign(const Ptr<CppFunction>& func, size_t n) {
	if (func->nargs != n) {
		throw_string("Wrong number of arguments");
		return;
	}
	auto args = gc.root(std::vector<Value>(n));
	for (size_t i = n; i-- > 0; ) {
		(*args)[i] = pop_data();
	}
	try {
		auto result = func->inner(gc, *args);
		push_data(*result);
	}
	catch (const Root<Value>& err) {
		push_data(*err);
		throw_();
	}
}

void VM::return_() {
	const auto& frame = call_stack.back();
	assert(data_stack.size() > frame.data_bottom && "Data stack underflow");
	auto value = pop_data();
	while (data_stack.size() > frame.data_bottom) {
		pop_data();
	}
	push_data(value);
	exception_stack.erase(
		exception_stack.begin() + frame.exception_bottom,
		exception_stack.end());
	call_stack.pop_back();
}

void VM::jump(size_t addr) {
	call_stack.back().ip = addr;
}

void VM::jump_cond(size_t addr, bool cond) {
	pop_data().visit(Overloaded {
		[&](bool b) {
			if (b == cond) {
				call_stack.back().ip = addr;
			}
		},
		[&](const auto&) {
			throw_string("Expected bool in conditional");
		}
	});
}

void VM::throw_() {
	if (exception_stack.size() == 0) {
		auto value = pop_data();
		while (data_stack.size() > 0) {
			pop_data();
		}
		push_data(value);
		call_stack.clear();
		exception_thrown = true;
	}
	else {
		auto handler = exception_stack.back();
		exception_stack.pop_back();
		auto value = pop_data();
		while (data_stack.size() > handler.data_bottom) {
			pop_data();
		}
		push_data(value);
		call_stack.erase(
			call_stack.begin() + handler.call_bottom,
			call_stack.end());
		call_stack.back().ip = handler.address;
	}
}

void VM::throw_string(const std::string& s) {
	push_data(*gc.alloc<std::string>(s));
	throw_();
}

void VM::catch_(size_t addr) {
	detail::ExceptionFrame handler;
	handler.data_bottom = data_stack.size();
	handler.call_bottom = call_stack.size();
	handler.address = addr;
	exception_stack.push_back(handler);
}

void VM::uncatch() {
	assert(exception_stack.size() > 0 && "Exception stack underflow");
	exception_stack.pop_back();
}
