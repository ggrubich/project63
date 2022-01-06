#pragma once

#include "gc.h"
#include "variant.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

// Basic instructions executed by the vm. Each one of them is documented
// with instruction arguments and a stack signature. For instance
//   GetVar(index), ( -- x )
// describes an opcode named GetVar with one instruction argument "index"
// which takes zero stack arguments and returns one stack result referred to
// as "x".
enum class Opcode : uint8_t {
	// Generic opcodes
	//
	// Nop(), ( -- )
	// Does nothing.
	Nop,
	// Pop(), ( x -- )
	// Removes the topmost value from the data stack.
	Pop,
	// Nip(), ( x y -- y )
	// Removes the second topmost value from the data stack.
	Nip,
	// Dup(), ( x -- x x )
	// Duplicates the topmost value on the data stack.
	Dup,

	// Local variable operations.
	// Local variables are stored on the data stack. Indices used by their opcodes
	// are relative, i.e. index 0 refers to the bottommost variable used by
	// the currently executing function. Accessing local variables from outer
	// functions is illegal.
	//
	// GetVar(index), ( -- x )
	// Retrieves a local variable from the given index and pushes it onto the stack.
	GetVar,
	// SetVar(index), ( x -- )
	// Pops the topmost value from the stack and assigns it to the local variable
	// at the given index.
	SetVar,

	// Constant opcodes
	// Each function has an immutable array of constants associated with it.
	//
	// GetConst(index), ( -- x )
	// Fetches a value from function's constant array and pushes it onto the stack.
	GetConst,

	// Upvalue opcodes
	// Upvalues are references to outer function's stack variables which remain
	// valid even after that function returns. This mechanism can be used to
	// create closures.
	// Each funciton has an array of associated upvalues - indices in this section
	// refer to upvalue array of the currently executing function unless specified
	// otherwise.
	//
	// GetUp(index), ( -- x )
	// Retrieves an upvalue from the given index and puts it on the stack.
	GetUp,
	// SetUp(index), ( x -- )
	// Assigns the topmost value from the stack to upvalue at the given index.
	SetUp,
	// ResetUp(), ( func -- func' )
	// Creates a clone of the function with an empty upvalue array.
	ResetUp,
	// MakeUp(index), ( func -- func )
	// Add a new upvalue to function on top of the stack. The upvalue will
	// point to local variable with the given index.
	MakeUp,
	// CopyUp(index), ( func -- func )
	// Copies an upvalue from the given index and adds it the function.
	CopyUp,

	// Instance variables (i.e. properties)
	//
	// GetProp(), ( obj name -- value )
	// Retrieves a property with the given name from object obj. Name must
	// be a string. If the property is not present, an exception is thrown.
	GetProp,
	// SetProp(), ( obj name value -- )
	// Assigns a value as obj's property with the given name. Name must be
	// a string. Trying to assign to a primitive object (like an int or bool)
	// will throw an exception.
	SetProp,

	// Function opcodes
	//
	// Call(), ( func x_1 x_2 x_3 ... x_n n -- y )
	// Calls a function with the given arguments.
	// Inside the function, the arguments will be assigned to local variables
	// at the bottom of function's stack segment (x_1 at index 0, x_2 at 1, etc).
	// After the call, function and arguments on the stack will be replaced with
	// function's result.
	Call,
	// Send(), ( obj msg -- result )
	// Sends the given message to the object. Sending will perform a method lookup
	// in obj's class and call the found value with obj as a self argument.
	// If the requested method is not present but obj has a "not_understood" method,
	// send will call not_understood with obj as self argument and then its result
	// with msg as an argument. Otherwise, an exception will be thrown.
	Send,

	// Flow control
	//
	// Return(), ( x -- )
	// Exits the current function with the given value and returns it
	// to the caller.
	Return,
	// Jump(addr), ( -- )
	// Performs an unconditional jump to the given address.
	Jump,
	// JumpIf(addr), ( bool -- )
	// If the topmost value is true performs a jump, otherwise does nothing.
	JumpIf,
	// JumpUnless(addr), ( bool -- )
	// If the topmost value is false performs a jump, otherwise does nothing.
	JumpUnless,

	// Exceptions
	//
	// Throw(), ( ex -- )
	// Throws a value from the top of the stack as an exception. The vm will pop
	// the first exception handler from the exception stack and use it to resume
	// execution. If the exception stack is empty, the vm itself will return
	// an error.
	Throw,
	// Catch(addr), ( -- )
	// Pushes an exception handler onto the exception stack. If an exception
	// is caught by the handler, the execution will resume at the given address
	// addr with the exception value on top of the stack.
	Catch,
	// Uncatch(), ( -- )
	// Pops a handler from the exception stack.
	Uncatch,
};

// VM instruction along with its argument.
struct Instruction {
	Opcode op : 5;
	uint32_t arg : 27;

	Instruction(Opcode op);
	Instruction(Opcode op, uint32_t arg);
};

struct Nil {};

struct Function;

struct CppFunction;

struct Object;

struct CppObject;

struct Klass;

struct Context;

// Union of all possible value types.
struct Value : Variant<
	Nil,
	bool,
	int64_t,
	Ptr<std::string>,
	Ptr<Function>,
	Ptr<CppFunction>,
	Ptr<Object>,
	Ptr<CppObject>,
	Ptr<Klass>
> {
	using Variant::Variant;

	Value();

	Ptr<Klass> class_of(Context&) const;
};

// A shared global context.
struct Context : public Collector {
	Root<Context*> this_root;

	Ptr<Klass> object_cls;
	Ptr<Klass> class_cls;
	Ptr<Klass> nil_cls;
	Ptr<Klass> bool_cls;
	Ptr<Klass> int_cls;
	Ptr<Klass> string_cls;
	Ptr<Klass> function_cls;

	Context();
};

// Upvalue is either an absolute index in the data stack (open upvalue)
// or a value itself (closed upvalue).
struct Upvalue : Variant<uint64_t, Value> {
	using Variant::Variant;
};

// Constant part of the function which can be shared by differendt closures.
struct FunctionProto {
	uint64_t nargs;
	std::vector<Instruction> code;
	std::vector<Value> constants;
};

// A function closure.
struct Function {
	Ptr<FunctionProto> proto;
	std::vector<Ptr<Upvalue>> upvalues;

	Function(const Ptr<FunctionProto>& proto);
};

// Foreign function implemented in C++.
struct CppFunction {
	using Inner = std::function<Root<Value>(Context&, const std::vector<Value>&)>;

	uint64_t nargs;
	Inner inner;

	CppFunction(uint64_t nargs, Inner inner);
};

// A native compound object.
// While all values are considered objects, this particular class name refers
// to dictionary-like objects creatable in external code.
struct Object {
	std::unordered_map<std::string, Value> properties;
	Ptr<Klass> klass;

	explicit Object(const Ptr<Klass>&);

	// Getter and setter for object properties.
	std::optional<Value> get_prop(const std::string& name) const;
	void set_prop(const std::string& name, const Value& value);
};

// Base class for foreign C++ objects.
struct CppObject {
	Ptr<Klass> klass;

	explicit CppObject(const Ptr<Klass>&);
	virtual ~CppObject() = default;
};

namespace detail {

struct MethodEntry {
	Value value;
	// True if entry is owned by the class, false if it's cached.
	bool own;
	// Detonator for invalidating caches.
	Ptr<bool> valid;
};

}  // namespace detail

// A class. We spell it with k to avoid conflicting with C++ keyword.
// Klass contains all members of Object along with a map of methods and
// a superclass chain.
struct Klass : Object {
	std::unordered_map<std::string, detail::MethodEntry> methods;
	std::optional<Ptr<Klass>> base;

	// Creates a class from raw parts.
	explicit Klass(const Ptr<Klass>& klass, const std::optional<Ptr<Klass>>& base);
	// Creates a class inherited from base.
	explicit Klass(Context& ctx, const Ptr<Klass>& base);

	// Finds a method in the class chain.
	std::optional<Value> lookup(const std::string& name);
	// Removes a method from the class and returns it.
	std::optional<Value> remove(const std::string& name);
	// Creates a new method or overwrites an existing one.
	void define(Context& ctx, const std::string& name, const Value& value);

private:
	std::optional<std::pair<Value, Ptr<bool>>> lookup_rec(const std::string& name);
	void define_fixup(Context& ctx, const std::string& name);
};

// Implementations

template<> struct Trace<Nil> {
	void operator()(const Nil&, Tracer&) {}
};

template<> struct Trace<Value> {
	void operator()(const Value& x, Tracer& t) {
		Trace<Value::Variant>{}(x, t);
	}
};

template<> struct Trace<Context> {
	void operator()(const Context& x, Tracer& t) {
		t(x.object_cls);
		t(x.class_cls);
		t(x.nil_cls);
		t(x.bool_cls);
		t(x.int_cls);
		t(x.string_cls);
		t(x.function_cls);
	}
};

template<> struct Trace<Upvalue> {
	void operator()(const Upvalue& x, Tracer& t) {
		Trace<Upvalue::Variant>{}(x, t);
	}
};

template<> struct Trace<FunctionProto> {
	void operator()(const FunctionProto& proto, Tracer& t) {
		Trace<decltype(proto.constants)>{}(proto.constants, t);
	}
};

template<> struct Trace<Function> {
	void operator()(const Function& f, Tracer& t) {
		t(f.proto);
		Trace<decltype(f.upvalues)>{}(f.upvalues, t);
	}
};

template<> struct Trace<CppFunction> {
	void operator()(const CppFunction&, Tracer&) {}
};

template<> struct Trace<Object> {
	void operator()(const Object& x, Tracer& t) {
		Trace<decltype(x.properties)>{}(x.properties, t);
		t(x.klass);
	}
};

template<> struct Trace<CppObject> {
	void operator()(const CppObject& x, Tracer& t) {
		t(x.klass);
	}
};

template<> struct Trace<detail::MethodEntry> {
	void operator()(const detail::MethodEntry& x, Tracer& t) {
		Trace<Value>{}(x.value, t);
		t(x.valid);
	}
};

template<> struct Trace<Klass> {
	void operator()(const Klass& x, Tracer& t) {
		Trace<Object>{}(x, t);
		Trace<decltype(x.methods)>{}(x.methods, t);
		Trace<decltype(x.base)>{}(x.base, t);
	}
};
