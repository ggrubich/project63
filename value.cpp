#include "value.h"

#include <cmath>
#include <iomanip>
#include <ios>
#include <sstream>

std::ostream& operator<<(std::ostream& s, Opcode op) {
	switch (op) {
	case Opcode::Nop:        s << "Nop"; break;
	case Opcode::Pop:        s << "Pop"; break;
	case Opcode::Nip:        s << "Nip"; break;
	case Opcode::Dup:        s << "Dup"; break;
	case Opcode::Nil:        s << "Nil"; break;
	case Opcode::GetVar:     s << "GetVar"; break;
	case Opcode::SetVar:     s << "SetVar"; break;
	case Opcode::GetConst:   s << "GetConst"; break;
	case Opcode::GetUp:      s << "GetUp"; break;
	case Opcode::SetUp:      s << "SetUp"; break;
	case Opcode::ResetUp:    s << "ResetUp"; break;
	case Opcode::MakeUp:     s << "MakeUp"; break;
	case Opcode::CopyUp:     s << "CopyUp"; break;
	case Opcode::GetProp:    s << "GetProp"; break;
	case Opcode::SetProp:    s << "SetProp"; break;
	case Opcode::Call:       s << "Call"; break;
	case Opcode::Send:       s << "Send"; break;
	case Opcode::Return:     s << "Return"; break;
	case Opcode::Jump:       s << "Jump"; break;
	case Opcode::JumpIf:     s << "JumpIf"; break;
	case Opcode::JumpUnless: s << "JumpUnless"; break;
	case Opcode::Throw:      s << "Throw"; break;
	case Opcode::Catch:      s << "Catch"; break;
	case Opcode::Uncatch:    s << "Uncatch"; break;
	}
	return s;
}

Instruction::Instruction(Opcode op) : Instruction(op, 0) {}

Instruction::Instruction(Opcode op, uint32_t arg)
	: op(op)
	, arg(arg)
{}

std::ostream& operator<<(std::ostream& s, Instruction instr) {
	s << instr.op;
	switch (instr.op) {
	case Opcode::GetVar:
	case Opcode::SetVar:
	case Opcode::GetConst:
	case Opcode::GetUp:
	case Opcode::SetUp:
	case Opcode::MakeUp:
	case Opcode::CopyUp:
	case Opcode::Jump:
	case Opcode::JumpIf:
	case Opcode::JumpUnless:
	case Opcode::Catch:
		s << " " << instr.arg;
		break;
	default:
		break;
	}
	return s;
}

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

namespace {

void inspect_string(std::ostream& buf, const std::string& str) {
	constexpr char escapes[] = {'a', 'b', 't', 'n', 'v', 'f', 'r'};
	buf << "\"";
	for (auto c : str) {
		if ('\a' <= c && c <= '\r') {
			buf << "\\" << escapes[c - '\a'];
		}
		else if (0x00 <= c && c <= 0x1f) {
			buf << "\\x" << std::setw(2) << std::setfill('0') << std::hex << int(c);
		}
		else if (c == '"' || c == '\\') {
			buf << "\\" << c;
		}
		else {
			buf << c;
		}
	}
	buf << "\"";
}

}  // namespace annonymous

std::string Value::inspect() const {
	std::stringstream buf;
	visit(Overloaded {
		[&](const Nil&) { buf << "nil"; },
		[&](const bool& b) { buf << (b ? "true" : "false"); },
		[&](const int64_t& n) { buf << n; },
		[&](const Ptr<std::string>& str) { inspect_string(buf, *str); },
		[&](const Ptr<Function>& x) { buf << "Function#" << x.address(); },
		[&](const Ptr<CppFunction>& x) { buf << "CppFunction#" << x.address(); },
		[&](const Ptr<Object>& x) { buf << "Object#" << x.address(); },
		[&](const Ptr<CppObject>& x) { buf << "CppObject#" << x.address(); },
		[&](const Ptr<Klass>& x) { buf << "Klass#" << x.address(); }
	});
	return buf.str();
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

std::string Function::dump() const {
	std::stringstream buffer;
	std::unordered_map<const void*, int64_t> cache;
	std::function<int64_t(const void*)> label = [&](auto ptr) {
		if (cache.count(ptr) == 0) {
			cache[ptr] = cache.size();
		}
		return cache[ptr];
	};
	dump_rec(buffer, label);
	return buffer.str();
}

void Function::dump_rec(
		std::ostream& buffer,
		std::function<int64_t(const void*)>& label) const
{
	buffer << "Function#" << label(this) << std::endl;
	buffer << "nargs: " << proto->nargs << std::endl;
	buffer << "nconstants: " << proto->constants.size() << std::endl;
	buffer << "code:" << std::endl;
	int addrw = std::log10(proto->code.size()) + 1;
	for (size_t i = 0; i < proto->code.size(); ++i) {
		buffer << "  " << std::setfill(' ') << std::setw(addrw) << i << "  ";
		auto instr = proto->code[i];
		buffer << instr;
		if (instr.op == Opcode::GetConst) {
			buffer << " (";
			auto& value = proto->constants[instr.arg];
			value.visit(Overloaded {
				[&](const Nil&) { buffer << value.inspect(); },
				[&](const bool&) { buffer << value.inspect(); },
				[&](const int64_t&) { buffer << value.inspect(); },
				[&](const Ptr<std::string>&) { buffer << value.inspect(); },
				[&](const auto& ptr) {
					auto tmp = value.inspect();
					buffer << tmp.substr(0, tmp.find('#')) <<
						"#" << label(ptr.address());
				}
			});
			buffer << ")";
		}
		buffer << std::endl;
	}
	for (auto& value : proto->constants) {
		if (value.holds<Ptr<Function>>()) {
			buffer << std::endl;
			value.get<Ptr<Function>>()->dump_rec(buffer, label);
		}
	}
}

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
