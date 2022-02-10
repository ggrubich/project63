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

void load_object(Context& ctx) {
	ctx.builtins["Object"] = ctx.object_cls;

	ctx.object_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
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
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
			auto res = ctx.vm.send_call(self, "==", args);
			return ctx.vm.send(*res, "!");
		}
	)));
	ctx.object_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto obj = coerce_object(ctx, args[0], "Object.hash");
			auto hash = obj.visit([](const auto& p) {
				return reinterpret_cast<int64_t>(p.address());
			});
			return ctx.g.root<Value>(hash);
		}
	)));

	ctx.object_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto obj = coerce_object(ctx, args[0], "Object.inspect");
			auto ptr = obj.visit([](const auto& p) -> void* {
				return p.address();
			});
			return Root<Value>(ctx.g.alloc(format("<Object#", ptr, ">")));
		}
	)));
	ctx.object_cls->define(ctx, "display", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			return ctx.vm.send(args[0], "inspect");
		}
	)));

	ctx.object_cls->define(ctx, "class", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto cls = args[0].class_of(ctx);
			return ctx.g.root<Value>(cls);
		}
	)));
	ctx.object_cls->define(ctx, "instance?", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
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
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
			auto msg = coerce_string(ctx, args[0], "Object.send");
			return ctx.vm.send(self, *msg);
		}
	)));

	ctx.object_cls->klass->define(ctx, "allocate", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto cls = coerce_class(ctx, args[0], "Object.class.allocate");
			return Root<Value>(ctx.g.alloc<Object>(cls));
		}
	)));
	ctx.object_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
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
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_class(ctx, self, "Class.==");
			auto res = args[0].holds<Ptr<Klass>>() &&
				x.address() == args[0].get<Ptr<Klass>>().address();
			return ctx.g.root<Value>(res);
		}
	)));
	ctx.class_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto x = coerce_class(ctx, args[0], "Class.hash");
			auto h = reinterpret_cast<int64_t>(x.address());
			return ctx.g.root<Value>(h);
		}
	)));

	ctx.class_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
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
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto cls = coerce_class(ctx, args[0], "Class.subclass");
			return Root<Value>(ctx.g.alloc<Klass>(ctx, cls));
		}
	)));
	ctx.class_cls->define(ctx, "superclass", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
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
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
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
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
			auto cls = coerce_class(ctx, self, "Class.define");
			auto name = coerce_string(ctx, args[0], "Class.define");
			auto value = args[1];
			cls->define(ctx, *name, value);
			return ctx.g.root<Value>();
		}
	)));
	ctx.class_cls->define(ctx, "undefine", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
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
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
			coerce_nil(ctx, self, "Nil.==");
			return ctx.g.root<Value>(args[0].holds<Nil>());
		}
	)));
	ctx.nil_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>&) {
			return ctx.g.root<Value>(int64_t(-1));
		}
	)));

	ctx.nil_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			coerce_nil(ctx, args[0], "Nil.inspect");
			return Root<Value>(ctx.g.alloc<std::string>("nil"));
		}
	)));

	ctx.nil_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>&) {
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
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_bool(ctx, self, "Bool.==");
			auto res = args[0].holds<bool>() && args[0].get<bool>() == x;
			return ctx.g.root<Value>(res);
		}
	)));
	ctx.bool_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto x = coerce_bool(ctx, args[0], "Bool.hash");
			return ctx.g.root<Value>(int64_t(x));
		}
	)));

	ctx.bool_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto x = coerce_bool(ctx, args[0], "Bool.inspect");
			return Root<Value>(ctx.g.alloc<std::string>(x ? "true" : "false"));
		}
	)));

	ctx.bool_cls->define(ctx, "!", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto x = coerce_bool(ctx, args[0], "Bool.!");
			return ctx.g.root<Value>(!x);
		}
	)));

	ctx.bool_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>&) {
			return Root<Value>(ctx.g.alloc<std::string>("Bool"));
		}
	)));
}

template<typename F>
Ptr<CppFunction> binary_int_op(Context& ctx, const char* where, F fun) {
	return *ctx.alloc(CppMethod(1,
		[where = where, fun = fun]
		(VMContext& ctx, const Value& self, const std::vector<Value>& args) {
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
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_int(ctx, self, "Int.==");
			auto res = args[0].holds<int64_t>() && x == args[0].get<int64_t>();
			return ctx.g.root<Value>(res);
		}
	)));
	ctx.int_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto x = coerce_int(ctx, args[0], "Int.hash");
			return ctx.g.root<Value>(x);
		}
	)));

	ctx.int_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto x = coerce_int(ctx, args[0], "Int.inspect");
			return Root<Value>(ctx.g.alloc(format(x)));
		}
	)));

	ctx.int_cls->define(ctx, "~", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
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
		[](VMContext& ctx, const std::vector<Value>&) {
			return Root<Value>(ctx.g.alloc<std::string>("Int"));
		}
	)));

	ctx.int_cls->klass->define(ctx, "max", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>&) {
			return ctx.g.root<Value>(std::numeric_limits<int64_t>::max());
		}
	)));
	ctx.int_cls->klass->define(ctx, "min", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>&) {
			return ctx.g.root<Value>(std::numeric_limits<int64_t>::min());
		}
	)));
}

void load_string(Context& ctx) {
	ctx.string_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["String"] = ctx.string_cls;

	ctx.string_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, self, "String.==");
			auto res = args[0].holds<Ptr<std::string>>() &&
				(*x == *args[0].get<Ptr<std::string>>());
			return ctx.g.root<Value>(res);
		}
	)));
	ctx.string_cls->define(ctx, "hash", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, args[0], "String.hash");
			auto h = static_cast<int64_t>(std::hash<std::string>{}(*x));
			return ctx.g.root<Value>(h);
		}
	)));

	ctx.string_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, args[0], "String.inspect");
			return Root<Value>(ctx.g.alloc(quote_string(*x)));
		}
	)));
	ctx.string_cls->define(ctx, "display", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, args[0], "String.display");
			return ctx.g.root<Value>(x);
		}
	)));

	ctx.string_cls->define(ctx, "++", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
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
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, args[0], "String.len");
			auto n = static_cast<int64_t>(x->size());
			return ctx.g.root<Value>(n);
		}
	)));
	ctx.string_cls->define(ctx, "get", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, self, "String.get");
			auto i = coerce_int(ctx, args[0], "String.get");
			if (i < 0 || i >= int64_t(x->size())) {
				error(ctx, "String.get: String index out of range");
			}
			auto y = x->substr(i, 1);
			return Root<Value>(ctx.g.alloc(std::move(y)));
		}
	)));
	ctx.string_cls->define(ctx, "substr", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
			auto x = coerce_string(ctx, self, "String.substr");
			auto a = coerce_int(ctx, args[0], "String.substr");
			auto b = coerce_int(ctx, args[1], "String.substr");
			if (a < 0) { a = 0; }
			if (b > int64_t(x->size())) { b = x->size(); }
			auto y = x->substr(a, b - a);
			return Root<Value>(ctx.g.alloc(std::move(y)));
		}
	)));

	ctx.string_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>&) {
			return Root<Value>(ctx.g.alloc<std::string>("String"));
		}
	)));
}

void load_function(Context& ctx) {
	ctx.function_cls = *ctx.alloc<Klass>(ctx, ctx.object_cls);

	ctx.builtins["Function"] = ctx.function_cls;

	ctx.function_cls->define(ctx, "==", *ctx.alloc(CppMethod(1,
		[](VMContext& ctx, const Value& self, const std::vector<Value>& args) {
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
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto func = coerce_function(ctx, args[0], "Function.hash");
			auto hash = func.visit([](const auto& p) {
				return reinterpret_cast<int64_t>(p.address());
			});
			return ctx.g.root<Value>(hash);
		}
	)));

	ctx.function_cls->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto func = coerce_function(ctx, args[0], "Function.inspect");
			auto ptr = func.visit([](const auto& p) -> void* {
				return p.address();
			});
			return Root<Value>(ctx.g.alloc(format("<Function#", ptr, ">")));
		}
	)));

	ctx.function_cls->klass->define(ctx, "inspect", *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>&) {
			return Root<Value>(ctx.g.alloc<std::string>("Function"));
		}
	)));
}

void load_auxiliary(Context& ctx) {
	ctx.builtins["print"] = *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
			auto repr = ctx.vm.send(args[0], "display");
			std::cout << *coerce_string(ctx, *repr, "print");
			return ctx.g.root<Value>();
		}
	));
	ctx.builtins["println"] = *ctx.alloc(CppLambda(1,
		[](VMContext& ctx, const std::vector<Value>& args) {
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
	load_function(ctx);

	load_auxiliary(ctx);
}
