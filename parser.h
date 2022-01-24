#pragma once

#include "variant.h"

#include <cstdint>
#include <memory>

struct Expression;

using ExpressionPtr = std::shared_ptr<Expression>;

struct StringExpr { std::string value; };
struct IntExpr { int64_t value; };
struct EmptyExpr {};

struct VariableExpr { std::string name; };
struct LetExpr { std::string name; ExpressionPtr value; };
struct AssignExpr { std::string name; ExpressionPtr value; };

struct GetPropExpr { ExpressionPtr obj; std::string name; };
struct SetPropExpr { ExpressionPtr obj; std::string name; ExpressionPtr value; };

struct CallExpr { ExpressionPtr func; std::vector<ExpressionPtr> args; };
struct SendExpr { ExpressionPtr obj; std::string msg; };

struct BlockExpr { std::vector<ExpressionPtr> exprs; };
struct IfExpr {
	std::vector<std::pair<ExpressionPtr, BlockExpr>> branches;
	std::optional<BlockExpr> otherwise;
};
struct WhileExpr {
	ExpressionPtr cond;
	BlockExpr body;
};
struct TryExpr {
	BlockExpr body;
	std::string error;
	BlockExpr handler;
};

struct LambdaExpr {
	std::vector<std::string> args;
	BlockExpr body;
};
struct MethodExpr {
	std::optional<std::vector<std::string>> args;
	BlockExpr body;
};

struct BreakExpr {};
struct ContinueExpr {};
struct ReturnExpr { ExpressionPtr value; };
struct ThrowExpr { ExpressionPtr value; };

struct Expression : Variant<
	StringExpr,
	IntExpr,
	EmptyExpr,

	VariableExpr,
	LetExpr,
	AssignExpr,

	GetPropExpr,
	SetPropExpr,

	CallExpr,
	SendExpr,

	BlockExpr,
	IfExpr,
	WhileExpr,
	TryExpr,

	LambdaExpr,
	MethodExpr,

	BreakExpr,
	ContinueExpr,
	ReturnExpr,
	ThrowExpr
> {
	using Variant::Variant;
};