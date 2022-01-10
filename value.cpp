#include "value.h"

Instruction::Instruction(Opcode op) : Instruction(op, 0) {}

Instruction::Instruction(Opcode op, uint32_t arg)
	: op(op)
	, arg(arg)
{}

Value::Value() : Variant(Nil()) {}

Ptr<Klass> Value::class_of(Context& ctx) const {
	return visit(Overloaded {
		[&](const Nil&) { return ctx.nil_cls; },
		[&](const bool&) { return ctx.bool_cls; },
		[&](const int64_t&) { return ctx.int_cls; },
		[&](const Ptr<std::string>&) { return ctx.string_cls; },
		[&](const Ptr<Function>&) { return ctx.function_cls; },
		[&](const Ptr<CppFunction>&) { return ctx.function_cls; },
		[&](const Ptr<Object>& obj) { return obj->klass; },
		[&](const Ptr<CppObject>& obj) { return obj->klass; },
		[&](const Ptr<Klass>& cls) { return cls->klass; }
	});
}

Context::Context()
	: this_root(root(this))
{
	// TODO: Add actual methods.
	object_cls = *alloc<Klass>(Ptr<Klass>(), std::nullopt);
	class_cls = *alloc<Klass>(Ptr<Klass>(), std::nullopt);
	object_cls->klass = *alloc<Klass>(class_cls, class_cls);
	class_cls->klass = class_cls;
	class_cls->base = object_cls;

	nil_cls = *alloc<Klass>(*this, object_cls);
	bool_cls = *alloc<Klass>(*this, object_cls);
	int_cls = *alloc<Klass>(*this, object_cls);
	string_cls = *alloc<Klass>(*this, object_cls);
	function_cls = *alloc<Klass>(*this, object_cls);
}

Function::Function(const Ptr<FunctionProto>& proto)
	: proto(proto)
	, upvalues()
{}

CppFunction::CppFunction(uint64_t nargs) : nargs(nargs) {}

Object::Object(const Ptr<Klass>& klass)
	: properties()
	, klass(klass)
{}

std::optional<Value> Object::get_prop(const std::string& name) const {
	auto it = properties.find(name);
	return it != properties.end() ? std::optional(it->second) : std::nullopt;
}

void Object::set_prop(const std::string& name, const Value& value) {
	properties.insert_or_assign(name, value);
}

CppObject::CppObject(const Ptr<Klass>& klass) : klass(klass) {}

Klass::Klass(const Ptr<Klass>& klass, const std::optional<Ptr<Klass>>& base)
	: Object(klass)
	, methods()
	, base(base)
{}

Klass::Klass(Context& ctx, const Ptr<Klass>& base)
	: Klass(*ctx.alloc<Klass>(base->klass->klass, base->klass), base)
{}

std::optional<Value> Klass::lookup(const std::string& name) {
	auto found = lookup_rec(name);
	return found ? std::optional(found->first) : std::nullopt;
}

std::optional<std::pair<Value, Ptr<bool>>>
Klass::lookup_rec(const std::string& name) {
	auto entry_it = methods.find(name);
	if (entry_it != methods.end()) {
		auto& entry = entry_it->second;
		if (entry.own || *entry.valid) {
			return std::pair(entry.value, entry.valid);
		}
		// Purge invalidated cache.
		methods.erase(entry_it);
	}
	auto found = base ? (*base)->lookup_rec(name) : std::nullopt;
	if (found) {
		detail::MethodEntry entry;
		entry.value = found->first;
		entry.own = false;
		entry.valid = found->second;
		methods.insert_or_assign(name, entry);
	}
	return found;
}

std::optional<Value> Klass::remove(const std::string& name) {
	auto entry_it = methods.find(name);
	if (entry_it != methods.end()) {
		auto& entry = entry_it->second;
		if (entry.own) {
			*entry.valid = false;
			auto value = entry.value;
			methods.erase(entry_it);
			return value;
		}
	}
	return std::nullopt;
}

void Klass::define(Context& ctx, const std::string& name, const Value& value) {
	// Simple path if we're changing an owned method.
	auto entry_it = methods.find(name);
	if (entry_it != methods.end()) {
		auto& entry = entry_it->second;
		if (entry.own) {
			entry.value = value;
			*entry.valid = false;
			entry.valid = *ctx.alloc<bool>(true);
			return;
		}
	}
	// Otherwise invalidate the inherited cache and insert a new method.
	if (base) {
		(*base)->define_fixup(ctx, name);
	}
	detail::MethodEntry entry;
	entry.value = value;
	entry.own = true;
	entry.valid = *ctx.alloc<bool>(true);
	methods.insert_or_assign(name, entry);
}

void Klass::define_fixup(Context& ctx, const std::string& name) {
	auto entry_it = methods.find(name);
	if (entry_it != methods.end()) {
		auto& entry = entry_it->second;
		if (entry.own) {
			*entry.valid = false;
			entry.valid = *ctx.alloc<bool>(true);
			return;
		}
		methods.erase(entry_it);
	}
	if (base) {
		(*base)->define_fixup(ctx, name);
	}
}
