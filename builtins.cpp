#include "builtins.h"

#include "strings.h"
#include "vm.h"

#include <algorithm>
#include <iostream>
#include <functional>
#include <limits>
#include <sstream>

namespace {

template<typename... Args>
std::string format(Args&&... args) {
	std::stringstream buf;
	(buf << ... << std::forward<Args>(args));
	return buf.str();
}

template<typename... Args>
[[noreturn]] void error(VMContext& ctx, Args&&... args) {
	throw Root<Value>(ctx.g.alloc(format(std::forward<Args>(args)...)));
}

template<typename T>
T coerce(VMContext& ctx, const Value& val, const char* where, const char* expected) {
	if (val.holds<T>()) {
		return val.get<T>();
	}
	else {
		auto actual = ctx.vm.send(val.class_of(ctx), "inspect")->visit(Overloaded {
			[&](const Ptr<std::string>& str) {
				return *str;
			},
			[&](const auto&) {
				return val.inspect();
			}
		});
		error(ctx, where, ": encountered ", actual, " instead of ", expected);
	}
}

void coerce_nil(VMContext& ctx, const Value& val, const char* where) {
	coerce<Nil>(ctx, val, where, "Nil");
}

bool coerce_bool(VMContext& ctx, const Value& val, const char* where) {
	return coerce<bool>(ctx, val, where, "Bool");
}

int64_t coerce_int(VMContext& ctx, const Value& val, const char* where) {
	return coerce<int64_t>(ctx, val, where, "Int");
}

Ptr<std::string> coerce_string(VMContext& ctx, const Value& val, const char* where) {
	return coerce<Ptr<std::string>>(ctx, val, where, "String");
}

Ptr<Array> coerce_array(VMContext& ctx, const Value& val, const char* where) {
	return coerce<Ptr<Array>>(ctx, val, where, "Array");
}

Variant<Ptr<Function>, Ptr<CppFunction>>
coerce_function(VMContext& ctx, const Value& val, const char* where) {
	try {
		return coerce<Ptr<Function>>(ctx, val, where, "Function");
	}
	catch (const Root<Value>&) {
		return coerce<Ptr<CppFunction>>(ctx, val, where, "Function");
	}
}

Variant<Ptr<Object>, Ptr<CppObject>>
coerce_object(VMContext& ctx, const Value& val, const char* where) {
	try {
		return coerce<Ptr<Object>>(ctx, val, where, "Object");
	}
	catch (const Root<Value>&) {
		return coerce<Ptr<CppObject>>(ctx, val, where, "Object");
	}
}

Ptr<Klass> coerce_class(VMContext& ctx, const Value& val, const char* where) {
	return coerce<Ptr<Klass>>(ctx, val, where, "Class");
}

// Retrieves an index in range [0, len].
size_t coerce_seq_uindex(VMContext& ctx, size_t len,
		const Value& val, const char* where)
{
	auto s = coerce_int(ctx, val, where);
	size_t u = s >= 0 ? s : (len + size_t(s));
	if (u > len) {
		error(ctx, where, ": index out of range");
	}
	return u;
}

// Retrieves an index in range [0, len).
size_t coerce_seq_index(VMContext& ctx, size_t len,
		const Value& val, const char* where)
{
	auto x = coerce_seq_uindex(ctx, len, val, where);
	if (x == len) {
		error(ctx, where, ": index out of range");
	}
	return x;
}

// Retrieves a numeric range [lower, upper).
std::pair<size_t, size_t>
coerce_seq_range(VMContext& ctx, size_t len,
		const Value& lower, const Value& upper, const char* where)
{
	auto clamp = [&](int64_t s) -> size_t {
		return s >= 0 ?
			std::min(size_t(s), len) :
			len - std::min(size_t(-s), len);
	};
	auto a = clamp(coerce_int(ctx, lower, where));
	auto b = clamp(coerce_int(ctx, upper, where));
	if (a > b) {
		b = a;
	}
	return std::pair{a, b};
}

void load_object(Context& ctx) {
	ctx.builtins["Object"] = ctx.object_cls;

	ctx.object_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto obj = coerce_object(ctx, self, "Object.==");
			auto res = std::visit(Overloaded {
				[](const Ptr<Object>& x, const Ptr<Object>& y) {
					return x.address() == y.address();
				},
				[](const Ptr<CppObject>& x, const Ptr<CppObject>& y) {
					return x.address() == y.address();
				},
				[](const auto&, const auto&) {
					return false;
				}
			}, obj.inner, args[0].inner);
			return ctx.g.root<Value>(res);
		}
	)));
	ctx.object_cls->define(ctx, "!=", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto res = ctx.vm.send_call(self, "==", args);
			return ctx.vm.send(*res, "!");
		}
	)));
	ctx.object_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto obj = coerce_object(ctx, args[0], "Object.hash");
			auto hash = obj.visit([](const auto& p) {
				return reinterpret_cast<int64_t>(p.address());
			});
			return ctx.g.root<Value>(hash);
		}
	)));

	ctx.object_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto obj = coerce_object(ctx, args[0], "Object.inspect");
			auto ptr = obj.visit([](const auto& p) -> void* {
				return p.address();
			});
			return Root<Value>(ctx.g.alloc(format("<Object#", ptr, ">")));
		}
	)));
	ctx.object_cls->define(ctx, "display", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			return ctx.vm.send(args[0], "inspect");
		}
	)));

	ctx.object_cls->define(ctx, "class", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto cls = args[0].class_of(ctx);
			return ctx.g.root<Value>(cls);
		}
	)));
	ctx.object_cls->define(ctx, "instance?", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto cls = self.class_of(ctx);
			auto base = coerce_class(ctx, args[0], "Object.instance?");
			while (true) {
				if (cls.address() == base.address()) {
					return ctx.g.root<Value>(true);
				}
				else if (!cls->base) {
					return ctx.g.root<Value>(false);
				}
				else {
					cls = *cls->base;
				}
			}
		}
	)));
	ctx.object_cls->define(ctx, "send", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto msg = coerce_string(ctx, args[0], "Object.send");
			return ctx.vm.send(self, *msg);
		}
	)));

	ctx.object_cls->klass->define(ctx, "allocate", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto cls = coerce_class(ctx, args[0], "Object.class.allocate");
			return Root<Value>(ctx.g.alloc<Object>(cls));
		}
	)));
	ctx.object_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_class(ctx, args[0], "Object.class.inspect");
			std::string str;
			if (x.address() == ctx.g.object_cls.address()) {
				str = "Object";
			}
			else {
				str = format("Object#", x.address());
			}
			return Root<Value>(ctx.g.alloc(std::move(str)));
		}
	)));
}

void load_class(Context& ctx) {
	ctx.builtins["Class"] = ctx.class_cls;

	ctx.class_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto x = coerce_class(ctx, self, "Class.==");
			auto res = args[0].holds<Ptr<Klass>>() &&
				x.address() == args[0].get<Ptr<Klass>>().address();
			return ctx.g.root<Value>(res);
		}
	)));
	ctx.class_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_class(ctx, args[0], "Class.hash");
			auto h = reinterpret_cast<int64_t>(x.address());
			return ctx.g.root<Value>(h);
		}
	)));

	ctx.class_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto cls = coerce_class(ctx, args[0], "Class.inspect");
			std::string str;
			if (cls.address() == ctx.g.class_cls.address()) {
				str = "Class";
			}
			else {
				str = format("Class#", cls.address());
			}
			return Root<Value>(ctx.g.alloc(std::move(str)));
		}
	)));

	ctx.class_cls->define(ctx, "subclass", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto cls = coerce_class(ctx, args[0], "Class.subclass");
			return Root<Value>(ctx.g.alloc<Klass>(ctx, cls));
		}
	)));
	ctx.class_cls->define(ctx, "superclass", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto cls = coerce_class(ctx, args[0], "Class.superclass");
			if (cls->base) {
				return ctx.g.root<Value>(*cls->base);
			}
			else {
				return ctx.g.root<Value>();
			}
		}
	)));

	ctx.class_cls->define(ctx, "lookup", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto cls = coerce_class(ctx, self, "Class.lookup");
			auto name = coerce_string(ctx, args[0], "Class.lookup");
			auto meth = cls->lookup(*name);
			if (meth) {
				return ctx.g.root(*meth);
			}
			else {
				return ctx.g.root<Value>();
			}
		}
	)));
	ctx.class_cls->define(ctx, "define", *ctx.alloc(CppMethod(2,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto cls = coerce_class(ctx, self, "Class.define");
			auto name = coerce_string(ctx, args[0], "Class.define");
			auto value = args[1];
			cls->define(ctx, *name, value);
			return ctx.g.root<Value>();
		}
	)));
	ctx.class_cls->define(ctx, "undefine", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto cls = coerce_class(ctx, self, "Class.undefine");
			auto name = coerce_string(ctx, args[0], "Class.undefine");
			cls->remove(*name);
			return ctx.g.root<Value>();
		}
	)));
}

void load_nil(Context& ctx) {
	ctx.nil_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["nil"] = Value(Nil());
	ctx.builtins["Nil"] = ctx.nil_cls;

	ctx.nil_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			coerce_nil(ctx, self, "Nil.==");
			return ctx.g.root<Value>(args[0].holds<Nil>());
		}
	)));
	ctx.nil_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array&) {
			return ctx.g.root<Value>(int64_t(-1));
		}
	)));

	ctx.nil_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			coerce_nil(ctx, args[0], "Nil.inspect");
			return Root<Value>(ctx.g.alloc<std::string>("nil"));
		}
	)));

	ctx.nil_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array&) {
			return Root<Value>(ctx.g.alloc<std::string>("Nil"));
		}
	)));
}

void load_bool(Context& ctx) {
	ctx.bool_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["true"] = Value(true);
	ctx.builtins["false"] = Value(false);
	ctx.builtins["Bool"] = ctx.bool_cls;

	ctx.bool_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto x = coerce_bool(ctx, self, "Bool.==");
			auto res = args[0].holds<bool>() && args[0].get<bool>() == x;
			return ctx.g.root<Value>(res);
		}
	)));
	ctx.bool_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_bool(ctx, args[0], "Bool.hash");
			return ctx.g.root<Value>(int64_t(x));
		}
	)));

	ctx.bool_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_bool(ctx, args[0], "Bool.inspect");
			return Root<Value>(ctx.g.alloc<std::string>(x ? "true" : "false"));
		}
	)));

	ctx.bool_cls->define(ctx, "!", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_bool(ctx, args[0], "Bool.!");
			return ctx.g.root<Value>(!x);
		}
	)));

	ctx.bool_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array&) {
			return Root<Value>(ctx.g.alloc<std::string>("Bool"));
		}
	)));
}

template<typename F>
Ptr<CppFunction> binary_int_op(Context& ctx, const char* where, F fun) {
	return *ctx.alloc(CppMethod(1,
		[where = where, fun = fun]
		(VMContext& ctx, const Value& self, const Array& args) {
			auto x = coerce_int(ctx, self, where);
			auto y = coerce_int(ctx, args[0], where);
			auto res = fun(ctx, x, y);
			return ctx.g.root<Value>(res);
		}
	));
}

void load_int(Context& ctx) {
	ctx.int_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["Int"] = ctx.int_cls;

	ctx.int_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto x = coerce_int(ctx, self, "Int.==");
			auto res = args[0].holds<int64_t>() && x == args[0].get<int64_t>();
			return ctx.g.root<Value>(res);
		}
	)));
	ctx.int_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_int(ctx, args[0], "Int.hash");
			return ctx.g.root<Value>(x);
		}
	)));

	ctx.int_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_int(ctx, args[0], "Int.inspect");
			return Root<Value>(ctx.g.alloc(format(x)));
		}
	)));

	ctx.int_cls->define(ctx, "~", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_int(ctx, args[0], "Int.~");
			if (x == INT64_MAX) {
				error(ctx, "Int overflow");
			}
			return ctx.g.root<Value>(-x);
		}
	)));
	ctx.int_cls->define(ctx, "+", binary_int_op(ctx, "Int.+",
		[](VMContext& ctx, int64_t x, int64_t y) {
			int64_t z;
			if (__builtin_add_overflow(x, y, &z)) {
				error(ctx, "Int overflow");
			}
			return z;
		}
	));
	ctx.int_cls->define(ctx, "-", binary_int_op(ctx, "Int.-",
		[](VMContext& ctx, int64_t x, int64_t y) {
			int64_t z;
			if (__builtin_sub_overflow(x, y, &z)) {
				error(ctx, "Int overflow");
			}
			return z;
		}
	));
	ctx.int_cls->define(ctx, "*", binary_int_op(ctx, "Int.*",
		[](VMContext& ctx, int64_t x, int64_t y) {
			int64_t z;
			if (__builtin_mul_overflow(x, y, &z)) {
				error(ctx, "Int overflow");
			}
			return z;
		}
	));
	ctx.int_cls->define(ctx, "/", binary_int_op(ctx, "Int./",
		[](VMContext& ctx, int64_t x, int64_t y) {
			if (y == 0) {
				error(ctx, "Division by zero");
			}
			return x / y;
		}
	));

	ctx.int_cls->define(ctx, "<", binary_int_op(ctx, "Int.<",
		[](VMContext&, int64_t x, int64_t y) { return x < y; }));
	ctx.int_cls->define(ctx, ">", binary_int_op(ctx, "Int.>",
		[](VMContext&, int64_t x, int64_t y) { return x > y; }));
	ctx.int_cls->define(ctx, "<=", binary_int_op(ctx, "Int.<=",
		[](VMContext&, int64_t x, int64_t y) { return x <= y; }));
	ctx.int_cls->define(ctx, ">=", binary_int_op(ctx, "Int.>=",
		[](VMContext&, int64_t x, int64_t y) { return x >= y; }));

	ctx.int_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array&) {
			return Root<Value>(ctx.g.alloc<std::string>("Int"));
		}
	)));

	ctx.int_cls->klass->define(ctx, "max", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array&) {
			return ctx.g.root<Value>(std::numeric_limits<int64_t>::max());
		}
	)));
	ctx.int_cls->klass->define(ctx, "min", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array&) {
			return ctx.g.root<Value>(std::numeric_limits<int64_t>::min());
		}
	)));
}

void load_string(Context& ctx) {
	ctx.string_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["String"] = ctx.string_cls;

	ctx.string_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto x = coerce_string(ctx, self, "String.==");
			auto res = args[0].holds<Ptr<std::string>>() &&
				(*x == *args[0].get<Ptr<std::string>>());
			return ctx.g.root<Value>(res);
		}
	)));
	ctx.string_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_string(ctx, args[0], "String.hash");
			auto h = static_cast<int64_t>(std::hash<std::string>{}(*x));
			return ctx.g.root<Value>(h);
		}
	)));

	ctx.string_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_string(ctx, args[0], "String.inspect");
			return Root<Value>(ctx.g.alloc(quote_string(*x)));
		}
	)));
	ctx.string_cls->define(ctx, "display", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_string(ctx, args[0], "String.display");
			return ctx.g.root<Value>(x);
		}
	)));

	ctx.string_cls->define(ctx, "++", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto x = coerce_string(ctx, self, "String.++");
			auto arg = ctx.g.root(args[0]);
			if (!arg->holds<Ptr<std::string>>()) {
				arg = ctx.vm.send(*arg, "display");
			}
			auto y = coerce_string(ctx, *arg, "String.++");
			return Root<Value>(ctx.g.alloc(*x + *y));
		}
	)));
	ctx.string_cls->define(ctx, "len", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto x = coerce_string(ctx, args[0], "String.len");
			auto n = static_cast<int64_t>(x->size());
			return ctx.g.root<Value>(n);
		}
	)));
	ctx.string_cls->define(ctx, "get", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto x = coerce_string(ctx, self, "String.get");
			auto i = coerce_seq_index(ctx, x->size(), args[0], "String.get");
			auto y = x->substr(i, 1);
			return Root<Value>(ctx.g.alloc(std::move(y)));
		}
	)));
	ctx.string_cls->define(ctx, "[]", *ctx.string_cls->lookup("get"));
	ctx.string_cls->define(ctx, "slice", *ctx.alloc(CppMethod(2,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto x = coerce_string(ctx, self, "String.substr");
			auto r = coerce_seq_range(ctx, x->size(),
					args[0], args[1], "String.substr");
			auto y = x->substr(r.first, r.second - r.first);
			return Root<Value>(ctx.g.alloc(std::move(y)));
		}
	)));

	ctx.string_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array&) {
			return Root<Value>(ctx.g.alloc<std::string>("String"));
		}
	)));
}

void load_array(Context& ctx) {
	ctx.array_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["Array"] = ctx.array_cls;

	ctx.array_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto xs = coerce_array(ctx, self, "Array.==");
			auto ys = coerce_array(ctx, args[0], "Array.==");
			auto res = std::equal(xs->begin(), xs->end(), ys->begin(), ys->end(),
				[&](const Value& x, const Value& y) {
					auto res = ctx.vm.send_call(x, "==", {y});
					return coerce_bool(ctx, *res, "Array.==");
				}
			);
			return ctx.g.root<Value>(res);
		}
	)));
	ctx.array_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto xs = coerce_array(ctx, args[0], "Array.hash");
			uint64_t result = 0;
			for (auto& x : *xs) {
				result += uint64_t(coerce_int(ctx, x, "Array.hash"));
			}
			return ctx.g.root<Value>(int64_t(result));
		}
	)));

	ctx.array_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto xs = coerce_array(ctx, args[0], "Array.inspect");
			std::stringstream buf;
			buf << "[";
			for (size_t i = 0; i < xs->size(); ++i) {
				auto s = ctx.vm.send((*xs)[i], "inspect");
				buf << *coerce_string(ctx, *s, "Array.inspect");
				if (i+1 < xs->size()) {
					buf << ", ";
				}
			}
			buf << "]";
			return Root<Value>(ctx.g.alloc(buf.str()));
		}
	)));

	ctx.array_cls->define(ctx, "len", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto arr = coerce_array(ctx, args[0], "Array.len");
			return ctx.g.root<Value>(int64_t(arr->size()));
		}
	)));
	ctx.array_cls->define(ctx, "empty?", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto arr = coerce_array(ctx, args[0], "Array.empty?");
			return ctx.g.root<Value>(arr->size() == 0);
		}
	)));
	ctx.array_cls->define(ctx, "first", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto arr = coerce_array(ctx, args[0], "Array.first");
			if (arr->size() == 0) {
				error(ctx, "Array.first: array is empty");
			}
			return ctx.g.root(arr->front());
		}
	)));
	ctx.array_cls->define(ctx, "last", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto arr = coerce_array(ctx, args[0], "Array.last");
			if (arr->size() == 0) {
				error(ctx, "Array.last: array is empty");
			}
			return ctx.g.root(arr->back());
		}
	)));
	ctx.array_cls->define(ctx, "get", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto arr = coerce_array(ctx, self, "Array.get");
			auto idx = coerce_seq_index(ctx, arr->size(), args[0], "Array.get");
			return ctx.g.root((*arr)[idx]);
		}
	)));
	ctx.array_cls->define(ctx, "[]", *ctx.array_cls->lookup("get"));
	ctx.array_cls->define(ctx, "slice", *ctx.alloc(CppMethod(2,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto arr = coerce_array(ctx, self, "Array.slice");
			auto range = coerce_seq_range(ctx, arr->size(),
					args[0], args[1], "Array.slice");
			auto slice = Array{arr->begin()+range.first, arr->begin()+range.second};
			return Root<Value>(ctx.g.alloc<Array>(std::move(slice)));
		}
	)));
	ctx.array_cls->define(ctx, "clone", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto arr = coerce_array(ctx, args[0], "Array.clone");
			return Root<Value>(ctx.g.alloc<Array>(*arr));
		}
	)));

	ctx.array_cls->define(ctx, "set", *ctx.alloc(CppMethod(2,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto arr = coerce_array(ctx, self, "Array.set");
			auto idx = coerce_seq_index(ctx, arr->size(), args[0], "Array.set");
			auto value = args[1];
			(*arr)[idx] = value;
			return ctx.g.root(self);
		}
	)));
	ctx.array_cls->define(ctx, "[]=", *ctx.array_cls->lookup("set"));
	ctx.array_cls->define(ctx, "insert", *ctx.alloc(CppMethod(2,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto arr = coerce_array(ctx, self, "Array.insert");
			auto idx = coerce_seq_uindex(ctx, arr->size(), args[0], "Array.insert");
			auto value = args[1];
			arr->insert(arr->begin() + idx, value);
			return ctx.g.root(self);
		}
	)));
	ctx.array_cls->define(ctx, "remove", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto arr = coerce_array(ctx, self, "Array.remove");
			auto idx = coerce_seq_index(ctx, arr->size(), args[0], "Array.remove");
			auto result = ctx.g.root((*arr)[idx]);
			arr->erase(arr->begin() + idx);
			return result;
		}
	)));
	ctx.array_cls->define(ctx, "push", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto arr = coerce_array(ctx, self, "Array.push");
			auto value = args[0];
			arr->emplace_back(value);
			return ctx.g.root(self);
		}
	)));
	ctx.array_cls->define(ctx, "pop", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto arr = coerce_array(ctx, args[0], "Array.pop");
			if (arr->size() == 0) {
				error(ctx, "Array.pop: array is empty");
			}
			auto result = ctx.g.root(arr->back());
			arr->pop_back();
			return result;
		}
	)));
	ctx.array_cls->define(ctx, "clear", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto arr = coerce_array(ctx, args[0], "Array.clear");
			arr->clear();
			return ctx.g.root(args[0]);
		}
	)));

	ctx.array_cls->define(ctx, "map", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto arr = coerce_array(ctx, self, "Array.map");
			auto func = args[0];
			for (size_t i = 0; i < arr->size(); ++i) {
				(*arr)[i] = *ctx.vm.call(func, {(*arr)[i]});
			}
			return ctx.g.root(self);
		}
	)));
	ctx.array_cls->define(ctx, "filter", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto arr = coerce_array(ctx, self, "Array.filter");
			auto func = args[0];
			size_t j = 0;
			for (auto& x : *arr) {
				auto keep = coerce_bool(ctx, *ctx.vm.call(func, {x}), "Array.filter");
				if (keep) {
					(*arr)[j] = x;
					j += 1;
				}
			}
			arr->erase(arr->begin() + j, arr->end());
			return ctx.g.root(self);
		}
	)));
	ctx.array_cls->define(ctx, "reverse", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto arr = coerce_array(ctx, args[0], "Array.reverse");
			std::reverse(arr->begin(), arr->end());
			return ctx.g.root(args[0]);
		}
	)));
	ctx.array_cls->define(ctx, "sort", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto arr = coerce_array(ctx, args[0], "Array.sort");
			std::sort(arr->begin(), arr->end(), [&](const auto& x, const auto& y) {
				auto res = ctx.vm.send_call(x, "<", {y});
				return coerce_bool(ctx, *res, "Array.sort");
			});
			return ctx.g.root(args[0]);
		}
	)));
	ctx.array_cls->define(ctx, "sort_by", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto arr = coerce_array(ctx, self, "Array.sort_by");
			auto func = args[0];
			std::sort(arr->begin(), arr->end(), [&](const auto& x, const auto& y) {
				auto res = ctx.vm.call(func, {x, y});
				return coerce_bool(ctx, *res, "Array.sort_by");
			});
			return ctx.g.root(self);
		}
	)));

	ctx.array_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array&) {
			return Root<Value>(ctx.g.alloc<std::string>("Array"));
		}
	)));

	ctx.array_cls->klass->define(ctx, "new", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array&) {
			return Root<Value>(ctx.g.alloc<Array>());
		}
	)));
}

void load_function(Context& ctx) {
	ctx.function_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["Function"] = ctx.function_cls;

	ctx.function_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto func = coerce_function(ctx, self, "Function.==");
			auto res = std::visit(Overloaded {
				[](const Ptr<Function>& x, const Ptr<Function>& y) {
					return x.address() == y.address();
				},
				[](const Ptr<CppFunction>& x, const Ptr<CppFunction>& y) {
					return x.address() == y.address();
				},
				[](const auto&, const auto&) {
					return false;
				}
			}, func.inner, args[0].inner);
			return ctx.g.root<Value>(res);
		}
	)));
	ctx.function_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto func = coerce_function(ctx, args[0], "Function.hash");
			auto hash = func.visit([](const auto& p) {
				return reinterpret_cast<int64_t>(p.address());
			});
			return ctx.g.root<Value>(hash);
		}
	)));

	ctx.function_cls->define(ctx, "apply", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const Array& args) {
			auto arr = coerce_array(ctx, args[0], "Function.apply");
			return ctx.vm.call(self, *arr);
		}
	)));

	ctx.function_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto func = coerce_function(ctx, args[0], "Function.inspect");
			auto ptr = func.visit([](const auto& p) -> void* {
				return p.address();
			});
			return Root<Value>(ctx.g.alloc(format("<Function#", ptr, ">")));
		}
	)));

	ctx.function_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array&) {
			return Root<Value>(ctx.g.alloc<std::string>("Function"));
		}
	)));
}

void load_auxiliary(Context& ctx) {
	ctx.builtins["print"] = *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto repr = ctx.vm.send(args[0], "display");
			std::cout << *coerce_string(ctx, *repr, "print");
			return ctx.g.root<Value>();
		}
	));
	ctx.builtins["println"] = *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const Array& args) {
			auto repr = ctx.vm.send(args[0], "display");
			std::cout << *coerce_string(ctx, *repr, "println") << std::endl;
			return ctx.g.root<Value>();
		}
	));
}

}  // namespace anonymous

void load_builtins(Context& ctx) {
	ctx.object_cls = *ctx.alloc<Klass>(Ptr<Klass>(), std::nullopt);
	ctx.class_cls = *ctx.alloc<Klass>(Ptr<Klass>(), std::nullopt);
	ctx.object_cls->klass = *ctx.alloc<Klass>(ctx.class_cls, ctx.class_cls);
	ctx.class_cls->klass = ctx.class_cls;
	ctx.class_cls->base = ctx.object_cls;
	load_object(ctx);
	load_class(ctx);

	load_nil(ctx);
	load_bool(ctx);
	load_int(ctx);
	load_string(ctx);
	load_array(ctx);
	load_function(ctx);

	load_auxiliary(ctx);
}
