#pragma once

#include "gc.h"
#include "value.h"

#include <optional>

namespace detail {

struct DataFrame {
	Value value;
	std::optional<Ptr<Upvalue>> upvalue;

	DataFrame(const Value&);
};

struct CallFrame {
	Ptr<Function> func;
	size_t ip;
	size_t data_bottom;
	size_t exception_bottom;
};

struct ExceptionFrame {
	size_t data_bottom;
	size_t call_bottom;
	size_t address;
};

}  // namespace detail

class VM {
private:
	Context& ctx;
	std::vector<detail::DataFrame> data_stack;
	std::vector<detail::CallFrame> call_stack;
	std::vector<detail::ExceptionFrame> exception_stack;
	bool exception_thrown;

	// Function called when an object doesn't have a requested method but has
	// not_understood method. Takes three arguments: (not_understood, obj, msg)
	// and effectively returns the value of not_understood(obj)(msg).
	Ptr<Function> send_fallback_fn;

public:
	// Constructs a vm tied to the given garbage collector instance.
	explicit VM(Context& ctx);

	void trace(Tracer& t) const;

	// Runs program's main function. Main takes no arguments.
	// If the program encounters an unhandled exception, run will throw
	// a Root<Value> as a C++ exception, otherwise main's result is returned.
	Root<Value> run(const Value& main);


private:
	Value remove_data(size_t off);
	Value pop_data();
	void nip_data();
	Value& get_data(size_t off);
	Value& peek_data();
	void push_data(const Value& value);

	void get_variable(size_t idx);
	void set_variable(size_t idx);

	void get_upvalue(size_t idx);
	void set_upvalue(size_t idx);
	void reset_upvalues();
	void make_upvalue(size_t idx);
	void copy_upvalue(size_t idx);

	void get_property();
	void set_property();

	void call();
	void call_native(const Ptr<Function>& func, size_t n);
	void call_foreign(const Ptr<CppFunction>& func, size_t n);
	void send();

	void return_();
	void jump(size_t addr);
	void jump_cond(size_t addr, bool cond);

	void throw_();
	void throw_string(const std::string& s);
	void catch_(size_t addr);
	void uncatch();
};

template<> struct Trace<VM> {
	void operator()(const VM& x, Tracer& t) {
		x.trace(t);
	}
};
