#pragma once

#include "variant.h"

#include <cstdint>
#include <memory>
#include <ostream>

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
struct UnaryExpr { std::string op; ExpressionPtr value; };
struct BinaryExpr { std::string op; ExpressionPtr lhs; ExpressionPtr rhs; };
struct AndExpr { ExpressionPtr lhs; ExpressionPtr rhs; };
struct OrExpr { ExpressionPtr lhs; ExpressionPtr rhs; };

struct BlockExpr { std::vector<ExpressionPtr> exprs; };
struct IfExpr {
	std::vector<std::pair<ExpressionPtr, std::vector<ExpressionPtr>>> branches;
	std::optional<std::vector<ExpressionPtr>> otherwise;
};
struct WhileExpr {
	ExpressionPtr cond;
	std::vector<ExpressionPtr> body;
};
struct TryExpr {
	std::vector<ExpressionPtr> body;
	std::string error;
	std::vector<ExpressionPtr> handler;
};

struct LambdaExpr {
	std::vector<std::string> args;
	std::vector<ExpressionPtr> body;
};
struct MethodExpr {
	std::optional<std::vector<std::string>> args;
	std::vector<ExpressionPtr> body;
};

struct BreakExpr {};
struct ContinueExpr {};
struct ReturnExpr { std::optional<ExpressionPtr> value; };
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
	UnaryExpr,
	BinaryExpr,
	AndExpr,
	OrExpr,

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

// Helper function for allocating ExpressionPtr values.
template<typename T, typename... Args>
ExpressionPtr make_expr(Args&&... args);
template<typename T>
ExpressionPtr make_expr(T&& x);

std::ostream& operator<<(std::ostream& s, const Expression& expr);

bool operator==(const Expression& e1, const Expression& e2);
bool operator!=(const Expression& e1, const Expression& e2);

struct ExpressionSeq {
	std::vector<ExpressionPtr> exprs;
};

std::ostream& operator<<(std::ostream& s, const ExpressionSeq& seq);

bool operator==(const ExpressionSeq& e1, const ExpressionSeq& e2);
bool operator!=(const ExpressionSeq& e1, const ExpressionSeq& e2);

// Parses a sequence of expressions from a string.
// Throws a runtime_error on failure.
ExpressionSeq parse_expr_seq(const std::string_view& input);

// Implementations

template<typename T, typename... Args>
ExpressionPtr make_expr(Args&&... args) {
	return std::make_shared<Expression>(T{std::forward<Args>(args)...});
}

template<typename T>
ExpressionPtr make_expr(T&& x) {
	return std::make_shared<Expression>(std::forward<T>(x));
}
