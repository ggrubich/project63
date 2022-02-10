#include "builtins.h"

#include "strings.h"
#include "vm.h"

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
[[noreturn]] void error(Context& ctx, Args&&... args) {
	throw Root<Value>(ctx.alloc(format(std::forward<Args>(args)...)));
}

template<typename T>
T coerce(Context& ctx, const Value& val, const char* where, const char* expected) {
	if (val.holds<T>()) {
		return val.get<T>();
	}
	else {
		error(ctx, "Unsupported operand ", val.inspect(),
			" in ", where, ", expecting ", expected);
	}
}

void coerce_nil(Context& ctx, const Value& val, const char* where) {
	coerce<Nil>(ctx, val, where, "Nil");
}

bool coerce_bool(Context& ctx, const Value& val, const char* where) {
	return coerce<bool>(ctx, val, where, "Bool");
}

int64_t coerce_int(Context& ctx, const Value& val, const char* where) {
	return coerce<int64_t>(ctx, val, where, "Int");
}

Ptr<std::string> coerce_string(Context& ctx, const Value& val, const char* where) {
	return coerce<Ptr<std::string>>(ctx, val, where, "String");
}

Ptr<Klass> coerce_class(Context& ctx, const Value& val, const char* where) {
	return coerce<Ptr<Klass>>(ctx, val, where, "Class");
}

void load_object(Context& ctx) {
	ctx.builtins["Object"] = ctx.object_cls;

	ctx.object_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto b = self.visit(Overloaded {
				[&](const Ptr<Object>& x) {
					return args[0].holds<Ptr<Object>>() &&
						x.address() == args[0].get<Ptr<Object>>().address();
				},
				[&](const Ptr<CppObject>& x) {
					return args[0].holds<Ptr<CppObject>>() &&
						x.address() == args[0].get<Ptr<CppObject>>().address();
				},
				[&](const auto&) {
					error(ctx, "Unsupported operand ", self.inspect(),
						" in Object.==, expecting Object");
					return false;
				}
			});
			return ctx.root<Value>(b);
		}
	)));
	ctx.object_cls->define(ctx, "!=", *ctx.alloc(CppMethod(1,
		[](Context&, VM& vm, const Value& self, const std::vector<Value>& args) {
			auto res = vm.send_call(self, "==", args);
			return vm.send(*res, "!");
		}
	)));
	ctx.object_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto ptr = args[0].visit(Overloaded {
				[](const Ptr<Object>& x) -> void* { return x.address(); },
				[](const Ptr<CppObject>& x) -> void* { return x.address(); },
				[&](const auto&) -> void* {
					error(ctx, "Unsupported operand ", args[0].inspect(),
						" in Object.hash expecting Object");
					return nullptr;
				}
			});
			return ctx.root<Value>(reinterpret_cast<int64_t>(ptr));
		}
	)));

	ctx.object_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto ptr = args[0].visit(Overloaded {
				[](const Ptr<Object>& x) -> void* { return x.address(); },
				[](const Ptr<CppObject>& x) -> void* { return x.address(); },
				[&](const auto&) -> void* {
					error(ctx, "Unsupported operand ", args[0].inspect(),
						" in Object.inspect expecting Object");
					return nullptr;
				}
			});
			return Root<Value>(ctx.alloc(format("<Object#", ptr, ">")));
		}
	)));
	ctx.object_cls->define(ctx, "display", *ctx.alloc(CppLambda(1,
		[](Context&, VM& vm, const std::vector<Value>& args) {
			return vm.send(args[0], "inspect");
		}
	)));

	ctx.object_cls->define(ctx, "class", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto cls = args[0].class_of(ctx);
			return ctx.root<Value>(cls);
		}
	)));
	ctx.object_cls->define(ctx, "instance?", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto cls = self.class_of(ctx);
			auto base = coerce_class(ctx, args[0], "Object.instance?");
			while (true) {
				if (cls.address() == base.address()) {
					return ctx.root<Value>(true);
				}
				else if (!cls->base) {
					return ctx.root<Value>(false);
				}
				else {
					cls = *cls->base;
				}
			}
		}
	)));
	ctx.object_cls->define(ctx, "send", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM& vm, const Value& self, const std::vector<Value>& args) {
			auto msg = coerce_string(ctx, args[0], "Object.send");
			return vm.send(self, *msg);
		}
	)));

	ctx.object_cls->klass->define(ctx, "allocate", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto cls = coerce_class(ctx, args[0], "Object.class.allocate");
			return Root<Value>(ctx.alloc<Object>(cls));
		}
	)));
	ctx.object_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_class(ctx, args[0], "Object.class.inspect");
			std::string str;
			if (x.address() == ctx.object_cls.address()) {
				str = "Object";
			}
			else {
				str = format("Object#", x.address());
			}
			return Root<Value>(ctx.alloc(std::move(str)));
		}
	)));
}

void load_class(Context& ctx) {
	ctx.builtins["Class"] = ctx.class_cls;

	ctx.class_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_class(ctx, self, "Class.==");
			auto res = args[0].holds<Ptr<Klass>>() &&
				x.address() == args[0].get<Ptr<Klass>>().address();
			return ctx.root<Value>(res);
		}
	)));
	ctx.class_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_class(ctx, args[0], "Class.hash");
			auto h = reinterpret_cast<int64_t>(x.address());
			return ctx.root<Value>(h);
		}
	)));

	ctx.class_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto cls = coerce_class(ctx, args[0], "Class.inspect");
			std::string str;
			if (cls.address() == ctx.class_cls.address()) {
				str = "Class";
			}
			else {
				str = format("Class#", cls.address());
			}
			return Root<Value>(ctx.alloc(std::move(str)));
		}
	)));

	ctx.class_cls->define(ctx, "subclass", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto cls = coerce_class(ctx, args[0], "Class.subclass");
			return Root<Value>(ctx.alloc<Klass>(ctx, cls));
		}
	)));
	ctx.class_cls->define(ctx, "superclass", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto cls = coerce_class(ctx, args[0], "Class.superclass");
			if (cls->base) {
				return ctx.root<Value>(*cls->base);
			}
			else {
				return ctx.root<Value>();
			}
		}
	)));

	ctx.class_cls->define(ctx, "lookup", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto cls = coerce_class(ctx, self, "Class.lookup");
			auto name = coerce_string(ctx, args[0], "Class.lookup");
			auto meth = cls->lookup(*name);
			if (meth) {
				return ctx.root(*meth);
			}
			else {
				return ctx.root<Value>();
			}
		}
	)));
	ctx.class_cls->define(ctx, "define", *ctx.alloc(CppMethod(2,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto cls = coerce_class(ctx, self, "Class.define");
			auto name = coerce_string(ctx, args[0], "Class.define");
			auto value = args[1];
			cls->define(ctx, *name, value);
			return ctx.root<Value>();
		}
	)));
	ctx.class_cls->define(ctx, "undefine", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto cls = coerce_class(ctx, self, "Class.undefine");
			auto name = coerce_string(ctx, args[0], "Class.undefine");
			cls->remove(*name);
			return ctx.root<Value>();
		}
	)));
}

void load_nil(Context& ctx) {
	ctx.nil_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["nil"] = Value(Nil());
	ctx.builtins["Nil"] = ctx.nil_cls;

	ctx.nil_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			coerce_nil(ctx, self, "Nil.==");
			return ctx.root<Value>(args[0].holds<Nil>());
		}
	)));
	ctx.nil_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>&) {
			return ctx.root<Value>(int64_t(-1));
		}
	)));

	ctx.nil_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			coerce_nil(ctx, args[0], "Nil.inspect");
			return Root<Value>(ctx.alloc<std::string>("nil"));
		}
	)));

	ctx.nil_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>&) {
			return Root<Value>(ctx.alloc<std::string>("Nil"));
		}
	)));
}

void load_bool(Context& ctx) {
	ctx.bool_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["true"] = Value(true);
	ctx.builtins["false"] = Value(false);
	ctx.builtins["Bool"] = ctx.bool_cls;

	ctx.bool_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_bool(ctx, self, "Bool.==");
			auto res = args[0].holds<bool>() && args[0].get<bool>() == x;
			return ctx.root<Value>(res);
		}
	)));
	ctx.bool_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_bool(ctx, args[0], "Bool.hash");
			return ctx.root<Value>(int64_t(x));
		}
	)));

	ctx.bool_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_bool(ctx, args[0], "Bool.inspect");
			return Root<Value>(ctx.alloc<std::string>(x ? "true" : "false"));
		}
	)));

	ctx.bool_cls->define(ctx, "!", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_bool(ctx, args[0], "Bool.!");
			return ctx.root<Value>(!x);
		}
	)));

	ctx.bool_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>&) {
			return Root<Value>(ctx.alloc<std::string>("Bool"));
		}
	)));
}

template<typename F>
Ptr<CppFunction> binary_int_op(Context& ctx, const char* where, F fun) {
	return *ctx.alloc(CppMethod(1,
		[where = where, fun = fun]
		(Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_int(ctx, self, where);
			auto y = coerce_int(ctx, args[0], where);
			auto res = fun(ctx, x, y);
			return ctx.root<Value>(res);
		}
	));
}

void load_int(Context& ctx) {
	ctx.int_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["Int"] = ctx.int_cls;

	ctx.int_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_int(ctx, self, "Int.==");
			auto res = args[0].holds<int64_t>() && x == args[0].get<int64_t>();
			return ctx.root<Value>(res);
		}
	)));
	ctx.int_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_int(ctx, args[0], "Int.hash");
			return ctx.root<Value>(x);
		}
	)));

	ctx.int_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_int(ctx, args[0], "Int.inspect");
			return Root<Value>(ctx.alloc(format(x)));
		}
	)));

	ctx.int_cls->define(ctx, "~", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_int(ctx, args[0], "Int.~");
			if (x == INT64_MAX) {
				error(ctx, "Int overflow");
			}
			return ctx.root<Value>(-x);
		}
	)));
	ctx.int_cls->define(ctx, "+", binary_int_op(ctx, "Int.+",
		[](Context& ctx, int64_t x, int64_t y) {
			int64_t z;
			if (__builtin_add_overflow(x, y, &z)) {
				error(ctx, "Int overflow");
			}
			return z;
		}
	));
	ctx.int_cls->define(ctx, "-", binary_int_op(ctx, "Int.-",
		[](Context& ctx, int64_t x, int64_t y) {
			int64_t z;
			if (__builtin_sub_overflow(x, y, &z)) {
				error(ctx, "Int overflow");
			}
			return z;
		}
	));
	ctx.int_cls->define(ctx, "*", binary_int_op(ctx, "Int.*",
		[](Context& ctx, int64_t x, int64_t y) {
			int64_t z;
			if (__builtin_mul_overflow(x, y, &z)) {
				error(ctx, "Int overflow");
			}
			return z;
		}
	));
	ctx.int_cls->define(ctx, "/", binary_int_op(ctx, "Int./",
		[](Context& ctx, int64_t x, int64_t y) {
			if (y == 0) {
				error(ctx, "Division by zero");
			}
			return x / y;
		}
	));

	ctx.int_cls->define(ctx, "<", binary_int_op(ctx, "Int.<",
		[](Context&, int64_t x, int64_t y) { return x < y; }));
	ctx.int_cls->define(ctx, ">", binary_int_op(ctx, "Int.>",
		[](Context&, int64_t x, int64_t y) { return x > y; }));
	ctx.int_cls->define(ctx, "<=", binary_int_op(ctx, "Int.<=",
		[](Context&, int64_t x, int64_t y) { return x <= y; }));
	ctx.int_cls->define(ctx, ">=", binary_int_op(ctx, "Int.>=",
		[](Context&, int64_t x, int64_t y) { return x >= y; }));

	ctx.int_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>&) {
			return Root<Value>(ctx.alloc<std::string>("Int"));
		}
	)));

	ctx.int_cls->klass->define(ctx, "max", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>&) {
			return ctx.root<Value>(std::numeric_limits<int64_t>::max());
		}
	)));
	ctx.int_cls->klass->define(ctx, "min", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>&) {
			return ctx.root<Value>(std::numeric_limits<int64_t>::min());
		}
	)));
}

void load_string(Context& ctx) {
	ctx.string_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["String"] = ctx.string_cls;

	ctx.string_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, self, "String.==");
			auto res = args[0].holds<Ptr<std::string>>() &&
				(*x == *args[0].get<Ptr<std::string>>());
			return ctx.root<Value>(res);
		}
	)));
	ctx.string_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, args[0], "String.hash");
			auto h = static_cast<int64_t>(std::hash<std::string>{}(*x));
			return ctx.root<Value>(h);
		}
	)));

	ctx.string_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, args[0], "String.inspect");
			return Root<Value>(ctx.alloc(quote_string(*x)));
		}
	)));
	ctx.string_cls->define(ctx, "display", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, args[0], "String.display");
			return ctx.root<Value>(x);
		}
	)));

	ctx.string_cls->define(ctx, "++", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM& vm, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, self, "String.++");
			auto arg = ctx.root(args[0]);
			if (!arg->holds<Ptr<std::string>>()) {
				arg = vm.send(*arg, "display");
			}
			auto y = coerce_string(ctx, *arg, "String.++");
			return Root<Value>(ctx.alloc(*x + *y));
		}
	)));
	ctx.string_cls->define(ctx, "len", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, args[0], "String.len");
			auto n = static_cast<int64_t>(x->size());
			return ctx.root<Value>(n);
		}
	)));
	ctx.string_cls->define(ctx, "get", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, self, "String.get");
			auto i = coerce_int(ctx, args[0], "String.get");
			if (i < 0 || i >= int64_t(x->size())) {
				error(ctx, "String index out of range");
			}
			auto y = x->substr(i, 1);
			return Root<Value>(ctx.alloc(std::move(y)));
		}
	)));
	ctx.string_cls->define(ctx, "substr", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, self, "String.substr");
			auto a = coerce_int(ctx, args[0], "String.substr");
			auto b = coerce_int(ctx, args[1], "String.substr");
			if (a < 0) { a = 0; }
			if (b > int64_t(x->size())) { b = x->size(); }
			auto y = x->substr(a, b - a);
			return Root<Value>(ctx.alloc(std::move(y)));
		}
	)));

	ctx.string_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>&) {
			return Root<Value>(ctx.alloc<std::string>("String"));
		}
	)));
}

void load_function(Context& ctx) {
	ctx.function_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["Function"] = ctx.function_cls;

	ctx.function_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](Context& ctx, VM&, const Value& self, const std::vector<Value>& args) {
			auto b = self.visit(Overloaded {
				[&](const Ptr<Function>& x) {
					return args[0].holds<Ptr<Function>>() &&
						x.address() == args[0].get<Ptr<Function>>().address();
				},
				[&](const Ptr<CppFunction>& x) {
					return args[0].holds<Ptr<CppFunction>>() &&
						x.address() == args[0].get<Ptr<CppFunction>>().address();
				},
				[&](const auto&) {
					error(ctx, "Unsupported operand ", self.inspect(),
						" in Function.==, expecting Function");
					return false;
				}
			});
			return ctx.root<Value>(b);
		}
	)));
	ctx.function_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto ptr = args[0].visit(Overloaded {
				[](const Ptr<Function>& x) -> void* { return x.address(); },
				[](const Ptr<CppFunction>& x) -> void* { return x.address(); },
				[&](const auto&) -> void* {
					error(ctx, "Unsupported operand ", args[0].inspect(),
						" in Function.hash expecting Function");
					return nullptr;
				}
			});
			return ctx.root<Value>(reinterpret_cast<int64_t>(ptr));
		}
	)));

	ctx.function_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>& args) {
			auto ptr = args[0].visit(Overloaded {
				[](const Ptr<Function>& x) -> void* { return x.address(); },
				[](const Ptr<CppFunction>& x) -> void* { return x.address(); },
				[&](const auto&) -> void* {
					error(ctx, "Unsupported operand ", args[0].inspect(),
						" in Function.inspect expecting Function");
					return nullptr;
				}
			});
			return Root<Value>(ctx.alloc(format("<Function#", ptr, ">")));
		}
	)));

	ctx.function_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM&, const std::vector<Value>&) {
			return Root<Value>(ctx.alloc<std::string>("Function"));
		}
	)));
}

void load_auxiliary(Context& ctx) {
	ctx.builtins["print"] = *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM& vm, const std::vector<Value>& args) {
			auto str = vm.send(args[0], "display");
			if (!str->holds<Ptr<std::string>>()) {
				error(ctx, "Wrong type returned from display");
			}
			std::cout << *str->get<Ptr<std::string>>();
			return ctx.root<Value>();
		}
	));
	ctx.builtins["println"] = *ctx.alloc(CppLambda(1,
		[](Context& ctx, VM& vm, const std::vector<Value>& args) {
			auto str = vm.send(args[0], "display");
			if (!str->holds<Ptr<std::string>>()) {
				error(ctx, "Wrong type returned from display");
			}
			std::cout << *str->get<Ptr<std::string>>() << std::endl;
			return ctx.root<Value>();
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
	load_function(ctx);

	load_auxiliary(ctx);
}
