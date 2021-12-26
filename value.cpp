#include "value.h"

Instruction::Instruction(Opcode op) : Instruction(op, 0) {}

Instruction::Instruction(Opcode op, uint32_t arg)
	: op(op)
	, arg(arg)
{}

Value::Value() : Variant(Nil()) {}

Function::Function(const Ptr<FunctionProto>& proto)
	: proto(proto)
	, upvalues()
{}

CppFunction::CppFunction(uint64_t nargs, Inner inner)
	: nargs(nargs)
	, inner(std::move(inner))
{}
