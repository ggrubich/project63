#include "vm.h"

#include <cassert>
#include <sstream>

namespace detail {

DataFrame::DataFrame(const Value& value)
	: value(value)
	, upvalue(std::nullopt)
{}

}  // namespace detaul

VM::VM(Context& ctx) : ctx(ctx) {
	auto _guard = ctx.root(this);
	send_fallback_fn = *ctx.alloc<Function>(*ctx.alloc<FunctionProto>());
	send_fallback_fn->proto->nargs = 3;
	send_fallback_fn->proto->code = std::vector<Instruction>{
		Instruction(Opcode::GetVar, 0),
		Instruction(Opcode::GetVar, 1),
		Instruction(Opcode::GetConst, 0),
		Instruction(Opcode::Call),
		Instruction(Opcode::GetVar, 2),
		Instruction(Opcode::GetConst, 0),
		Instruction(Opcode::Call),
		Instruction(Opcode::Return)
	};
	send_fallback_fn->proto->constants = std::vector<Value>{
		1
	};
}

void VM::trace(Tracer& t) const {
	for (const auto& x : data_stack) {
		Trace<Value>{}(x.value, t);
		Trace<decltype(x.upvalue)>{}(x.upvalue, t);

	}
	for (const auto& x : call_stack) {
		t(x.func);
	}
}

Root<Value> VM::call(const Value& func, const std::vector<Value>& args) {
	data_stack.clear();
	call_stack.clear();
	exception_stack.clear();
	exception_thrown = false;

	push_data(func);
	for (auto& arg : args) {
		push_data(arg);
	}
	push_data(int64_t(args.size()));
	call_();
	return run();
}

Root<Value> VM::send(const Value& obj, const std::string& msg) {
	data_stack.clear();
	call_stack.clear();
	exception_stack.clear();
	exception_thrown = false;

	push_data(obj);
	push_data(*ctx.alloc(msg));
	send_();
	return run();
}

Root<Value> VM::send_call(
		const Value& obj,
		const std::string& msg,
		const std::vector<Value>& args)
{
	auto func = send(obj, msg);
	return call(*func, args);
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
	auto func1 = ctx.root(pop_data().get<Ptr<Function>>());
	auto func2 = ctx.alloc<Function>((*func1)->proto);
	push_data(*func2);
}

void VM::make_upvalue(size_t idx) {
	assert(peek_data().holds<Ptr<Function>>() &&
			"Accessing upvalues on non-function");
	idx = call_stack.back().data_bottom + idx;
	assert(idx < data_stack.size() && "Variable out of range");
	if (!data_stack[idx].upvalue) {
		data_stack[idx].upvalue = *ctx.alloc<Upvalue>(idx);
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

void VM::get_property() {
	assert(peek_data().holds<Ptr<std::string>>() && "Prop name is not a string");
	auto name = pop_data().get<Ptr<std::string>>();
	auto obj = pop_data();
	auto value = obj.visit(Overloaded {
		[&](const Ptr<Object>& obj) {
			return obj->get_prop(*name);
		},
		[&](const Ptr<Klass>& cls) {
			return cls->get_prop(*name);
		},
		[](const auto&) {
			return std::optional<Value>();
		}
	});
	if (value) {
		push_data(*value);
	}
	else {
		std::stringstream buf;
		buf << "Property `" << *name << "` not found";
		throw_string(buf.str());
	}
}

void VM::set_property() {
	auto value = pop_data();
	assert(peek_data().holds<Ptr<std::string>>() && "Prop name is not a string");
	auto name = pop_data().get<Ptr<std::string>>();
	auto obj = pop_data();
	obj.visit(Overloaded {
		[&](const Ptr<Object>& obj) {
			obj->set_prop(*name, value);
		},
		[&](const Ptr<Klass>& cls) {
			cls->set_prop(*name, value);
		},
		[&](const auto&) {
			throw_string("Can't set property on a primitive value");
		}
	});
}

void VM::call_() {
	size_t n = pop_data().get<int64_t>();
	auto func = ctx.root(remove_data(n));
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
	auto args = ctx.root(std::vector<Value>(n));
	for (size_t i = n; i-- > 0; ) {
		(*args)[i] = pop_data();
	}
	try {
		auto result = (*func)(ctx, *args);
		push_data(*result);
	}
	catch (const Root<Value>& err) {
		push_data(*err);
		throw_();
	}
}

void VM::send_() {
	assert(peek_data().holds<Ptr<std::string>>() && "Message is not a string");
	auto msg = pop_data().get<Ptr<std::string>>();
	auto obj = pop_data();
	auto cls = obj.class_of(ctx);
	if (auto meth = cls->lookup(*msg)) {
		push_data(*meth);
		push_data(obj);
		push_data(1);
		call_();
	}
	else if (auto not_understood = cls->lookup("not_understood")) {
		push_data(*not_understood);
		push_data(obj);
		push_data(msg);
		call_native(send_fallback_fn, 3);
	}
	else {
		std::stringstream buf;
		buf << "Message `" << *msg << "` could not be handled";
		throw_string(buf.str());
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
	push_data(*ctx.alloc<std::string>(s));
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

Root<Value> VM::run() {
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
		case Opcode::Nil:
			push_data(Nil());
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

		case Opcode::GetProp:
			get_property();
			break;
		case Opcode::SetProp:
			set_property();
			break;

		case Opcode::Call:
			call_();
			break;
		case Opcode::Send:
			send_();
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
	auto result = ctx.root(pop_data());
	if (exception_thrown) {
		throw result;
	} else {
		return result;
	}
}
