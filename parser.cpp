#include "parser.h"

#include "strings.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

// operator<< for expressions

namespace {

struct Indent {
	size_t level;

	Indent operator+(size_t n) const;
};

Indent Indent::operator+(size_t n) const {
	return Indent{level + n};
}

std::ostream& operator<<(std::ostream& s, const Indent& indent) {
	for (size_t i = 0; i < indent.level; ++i) {
		s << "    ";
	}
	return s;
}

std::ostream& show_expr(std::ostream& s, Indent indent, const Expression& expr) {
	expr.visit(Overloaded {
		[&](const StringExpr& expr) {
			s << indent << "String{" << quote_string(expr.value) << "}";
		},
		[&](const IntExpr& expr) {
			s << indent << "Int{" << expr.value << "}";
		},
		[&](const EmptyExpr&) {
			s << indent << "Empty{}";
		},

		[&](const VariableExpr& expr) {
			s << indent << "Variable{" << expr.name << "}";
		},
		[&](const LetExpr& expr) {
			s << indent << "Let{\n";
			s << (indent+1) << expr.name << ",\n";
			show_expr(s, indent+1, *expr.value) << ",\n";
			s << indent << "}";
		},
		[&](const AssignExpr& expr) {
			s << indent << "Assign{\n";
			s << (indent+1) << expr.name << ",\n";
			show_expr(s, indent+1, *expr.value) << ",\n";
			s << indent << "}";
		},

		[&](const GetPropExpr& expr) {
			s << indent << "GetProp{\n";
			show_expr(s, indent+1, *expr.obj) << ",\n";
			s << (indent+1) << "@" << expr.name << ",\n";
			s << indent << "}";
		},
		[&](const SetPropExpr& expr) {
			s << indent << "SetProp{\n";
			show_expr(s, indent+1, *expr.obj) << ",\n";
			s << (indent+1) << "@" << expr.name << ",\n";
			show_expr(s, indent+1, *expr.value) << ",\n";
			s << indent << "}";
		},

		[&](const CallExpr& expr) {
			s << indent << "Call{\n";
			show_expr(s, indent+1, *expr.func) << ",\n";
			for (auto& e : expr.args) {
				show_expr(s, indent+1, *e) << ",\n";
			}
			s << indent << "}";
		},
		[&](const SendExpr& expr) {
			s << indent << "Send{\n";
			show_expr(s, indent+1, *expr.obj) << ",\n";
			s << (indent+1) << expr.msg << ",\n";
			s << indent << "}";
		},
		[&](const UnaryExpr& expr) {
			s << indent << "Unary{\n";
			s << (indent+1) << expr.op << ",\n";
			show_expr(s, indent+1, *expr.value) << ",\n";
			s << indent << "}";
		},
		[&](const BinaryExpr& expr) {
			s << indent << "Binary{\n";
			s << (indent+1) << expr.op << ",\n";
			show_expr(s, indent+1, *expr.lhs) << ",\n";
			show_expr(s, indent+1, *expr.rhs) << ",\n";
			s << indent << "}";
		},
		[&](const AndExpr& expr) {
			s << indent << "And{\n";
			show_expr(s, indent+1, *expr.lhs) << ",\n";
			show_expr(s, indent+1, *expr.rhs) << ",\n";
			s << indent << "}";
		},
		[&](const OrExpr& expr) {
			s << indent << "Or{\n";
			show_expr(s, indent+1, *expr.lhs) << ",\n";
			show_expr(s, indent+1, *expr.rhs) << ",\n";
			s << indent << "}";
		},

		[&](const BlockExpr& expr) {
			s << indent << "Block{\n";
			for (auto& e : expr.exprs) {
				show_expr(s, indent+1, *e) << ",\n";
			}
			s << indent << "}";
		},
		[&](const IfExpr& expr) {
			s << indent << "If{\n";
			for (auto& branch : expr.branches) {
				s << (indent+1) << "[\n";
				show_expr(s, indent+2, *branch.first) << ",\n";
				for (auto& e : branch.second) {
					show_expr(s, indent+2, *e) << ",\n";
				}
				s << (indent+1) << "],\n";
			}
			if (expr.otherwise) {
				s << (indent+1) << "[\n";
				s << (indent+2) << "otherwise,\n";
				for (auto& e : *expr.otherwise) {
					show_expr(s, indent+2, *e) << ",\n";
				}
				s << (indent+1) << "],\n";
			}
			s << indent << "}";
		},
		[&](const WhileExpr& expr) {
			s << indent << "While{\n";
			show_expr(s, indent+1, *expr.cond) << ",\n";
			for (auto& e : expr.body) {
				show_expr(s, indent+1, *e) << ",\n";
			}
			s << indent << "}";
		},
		[&](const TryExpr& expr) {
			s << indent << "Try{\n";
			s << (indent+1) << "[\n";
			for (auto& e : expr.body) {
				show_expr(s, indent+2, *e) << ",\n";
			}
			s << (indent+1) << "],\n";
			s << (indent+1) << expr.error << ",\n";
			s << (indent+1) << "[\n";
			for (auto& e : expr.handler) {
				show_expr(s, indent+2, *e) << ",\n";
			}
			s << (indent+1) << "],\n";
			s << indent << "}";
		},

		[&](const LambdaExpr& expr) {
			s << indent << "Lambda{\n";
			s << (indent+1) << "[";
			for (size_t i = 0; i < expr.args.size(); ++i) {
				s << expr.args[i];
				if (i+1 < expr.args.size()) {
					s << ", ";
				}
			}
			s << "],\n";
			for (auto& e : expr.body) {
				show_expr(s, indent+1, *e) << ",\n";
			}
			s << indent << "}";
		},
		[&](const MethodExpr& expr) {
			s << indent << "Method{\n";
			if (expr.args) {
				s << (indent+1) << "[";
				for (size_t i = 0; i < expr.args->size(); ++i) {
					s << (*expr.args)[i];
					if (i+1 < expr.args->size()) {
						s << ", ";
					}
				}
				s << "],\n";
			}
			for (auto& x : expr.body) {
				show_expr(s, indent+1, *x) << ",\n";
			}
			s << indent << "}";
		},

		[&](const BreakExpr&) {
			s << indent << "Break{}";
		},
		[&](const ContinueExpr&) {
			s << indent << "Continue{}";
		},
		[&](const ReturnExpr& expr) {
			if (expr.value) {
				s << indent << "Return{\n";
				show_expr(s, indent+1, **expr.value) << "\n";
				s << indent << "}";
			}
			else {
				s << indent << "Return{}";
			}
		},
		[&](const ThrowExpr& expr) {
			s << indent << "Throw{\n";
			show_expr(s, indent+1, *expr.value) << "\n";
			s << indent << "}";
		}
	});
	return s;
}

}  // namespace anonymous

std::ostream& operator<<(std::ostream& s, const Expression& expr) {
	return show_expr(s, Indent{0}, expr);
}

std::ostream& operator<<(std::ostream& s, const ExpressionSeq& seq) {
	for (size_t i = 0; i < seq.exprs.size(); ++i) {
		show_expr(s, Indent{0}, *seq.exprs[i]);
		if (i+1 < seq.exprs.size()) {
			s << ",\n";
		}
	}
	return s;
}

// operator==/!= for expressions

namespace {

bool all_equal(const std::vector<ExpressionPtr>& xs, const std::vector<ExpressionPtr>& ys) {
	return std::equal(xs.begin(), xs.end(), ys.begin(), ys.end(),
			[](auto& x, auto& y) { return *x == *y; });
}

}  // namespace anonymous

bool operator==(const Expression& e1, const Expression& e2) {
	return std::visit(Overloaded {
		[](const StringExpr& x, const StringExpr& y) { return x.value == y.value; },
		[](const IntExpr& x, const IntExpr& y) { return x.value == y.value; },
		[](const EmptyExpr&, const EmptyExpr&) { return true; },
		[](const VariableExpr& x, const VariableExpr& y) { return x.name == y.name; },
		[](const LetExpr& x, const LetExpr& y) {
			return x.name == y.name && (*x.value == *y.value);
		},
		[](const AssignExpr& x, const AssignExpr& y) {
			return x.name == y.name && (*x.value == *y.value);
		},
		[](const GetPropExpr& x, const GetPropExpr& y) {
			return (*x.obj == *y.obj) && x.name == y.name;
		},
		[](const SetPropExpr& x, const SetPropExpr& y) {
			return (*x.obj == *y.obj) && x.name == y.name && (*x.value == *y.value);
		},
		[](const CallExpr& x, const CallExpr& y) {
			return (*x.func == *y.func) && all_equal(x.args, y.args);
		},
		[](const SendExpr& x, const SendExpr& y) {
			return (*x.obj == *y.obj) && x.msg == y.msg;
		},
		[](const UnaryExpr& x, const UnaryExpr& y) {
			return x.op == y.op && (*x.value == *y.value);
		},
		[](const BinaryExpr& x, const BinaryExpr& y) {
			return x.op == y.op && (*x.lhs == *y.lhs) && (*x.rhs == *y.rhs);
		},
		[](const AndExpr& x, const AndExpr& y) {
			return (*x.lhs == *y.lhs) && (*x.rhs == *y.rhs);
		},
		[](const OrExpr& x, const OrExpr& y) {
			return (*x.lhs == *y.lhs) && (*x.rhs == *y.rhs);
		},
		[](const BlockExpr& x, const BlockExpr& y) {
			return all_equal(x.exprs, y.exprs);
		},
		[](const IfExpr& x, const IfExpr& y) {
			auto t1 = std::equal(
				x.branches.begin(), x.branches.end(),
				y.branches.begin(), y.branches.end(),
				[](auto& a, auto& b) {
					return (*a.first == *b.first) && all_equal(a.second, b.second);
				}
			);
			auto t2 = (!x.otherwise && !y.otherwise) ||
				(x.otherwise && y.otherwise && all_equal(*x.otherwise, *y.otherwise));
			return t1 && t2;
		},
		[](const WhileExpr& x, const WhileExpr& y) {
			return (*x.cond == *y.cond) && all_equal(x.body, y.body);
		},
		[](const TryExpr& x, const TryExpr& y) {
			return all_equal(x.body, y.body) &&
				x.error == y.error &&
				all_equal(x.handler, y.handler);
		},
		[](const LambdaExpr& x, const LambdaExpr& y) {
			return x.args == y.args && all_equal(x.body, y.body);
		},
		[](const MethodExpr& x, const MethodExpr& y) {
			return x.args == y.args && all_equal(x.body, y.body);
		},
		[](const BreakExpr&, const BreakExpr&) { return true; },
		[](const ContinueExpr&, const ContinueExpr&) { return true; },
		[](const ReturnExpr& x, const ReturnExpr& y) {
			return (!x.value && !y.value) ||
				(x.value && y.value && (**x.value == **y.value));
		},
		[](const ThrowExpr& x, const ThrowExpr& y) { return *x.value == *y.value; },
		[](const auto&, const auto&) { return false; }
	}, e1.inner, e2.inner);
}

bool operator!=(const Expression& e1, const Expression& e2) {
	return !(e1 == e2);
}

bool operator==(const ExpressionSeq& e1, const ExpressionSeq& e2) {
	return all_equal(e1.exprs, e2.exprs);
}

bool operator!=(const ExpressionSeq& e1, const ExpressionSeq& e2) {
	return !(e1 == e2);
}

namespace {

// tokenizer

using namespace std::literals;

enum class TokenType {
	Eof,
	Unknown,
	String,
	Int,
	Identifier,
	Operator,

	// keywords
	Let,
	If,
	Else,
	While,
	Try,
	Catch,
	Fn,
	Method,
	Break,
	Continue,
	Return,
	Throw,

	// symbols
	Equals,     // =
	At,         // @
	Dot,        // .
	Comma,      // ,
	LBrace,     // {
	RBrace,     // }
	Semicolon,  // ;
	LParen,     // (
	RParen,     // )
	And,        // &&
	Or,         // ||
};

struct Token {
	TokenType type;
	std::string_view str;
};

std::ostream& operator<<(std::ostream& s, TokenType type) {
	switch (type) {
	case TokenType::Eof:        s << "end of file"; break;
	case TokenType::Unknown:    s << "unknown character"; break;
	case TokenType::String:     s << "string"; break;
	case TokenType::Int:        s << "int"; break;
	case TokenType::Identifier: s << "identifier"; break;
	case TokenType::Operator:   s << "operator"; break;
	case TokenType::Let:        s << "keyword let"; break;
	case TokenType::If:         s << "keyword if"; break;
	case TokenType::Else:       s << "keyword else"; break;
	case TokenType::While:      s << "keyword while"; break;
	case TokenType::Try:        s << "keyword try"; break;
	case TokenType::Catch:      s << "keyword catch"; break;
	case TokenType::Fn:         s << "keyword fn"; break;
	case TokenType::Method:     s << "keyword method"; break;
	case TokenType::Break:      s << "keyword break"; break;
	case TokenType::Continue:   s << "keyword continue"; break;
	case TokenType::Return:     s << "keyword return"; break;
	case TokenType::Throw:      s << "keyword throw"; break;
	case TokenType::Equals:     s << "equals sign (=)"; break;
	case TokenType::At:         s << "at sign (@)"; break;
	case TokenType::Dot:        s << "dot (.)"; break;
	case TokenType::Comma:      s << "comma (,)"; break;
	case TokenType::LBrace:     s << "left brace ({)"; break;
	case TokenType::RBrace:     s << "right brace (})"; break;
	case TokenType::Semicolon:  s << "semicolon (;)"; break;
	case TokenType::LParen:     s << "left parenthesis (()"; break;
	case TokenType::RParen:     s << "right parenthesis ())"; break;
	case TokenType::And:        s << "and operator (&&)"; break;
	case TokenType::Or:         s << "or operator (||)"; break;
	}
	return s;
}

std::ostream& operator<<(std::ostream& s, const Token& tok) {
	s << tok.type;
	switch (tok.type) {
	case TokenType::Unknown:
	case TokenType::String:
	case TokenType::Int:
	case TokenType::Identifier:
	case TokenType::Operator:
		s << " " << tok.str;
		break;
	default:
		break;
	}
	return s;
}

constexpr auto known_keywords = std::array{
	std::pair{"let"sv, TokenType::Let},
	std::pair{"if"sv, TokenType::If},
	std::pair{"else"sv, TokenType::Else},
	std::pair{"while"sv, TokenType::While},
	std::pair{"try"sv, TokenType::Try},
	std::pair{"catch"sv, TokenType::Catch},
	std::pair{"fn"sv, TokenType::Fn},
	std::pair{"method"sv, TokenType::Method},
	std::pair{"break"sv, TokenType::Break},
	std::pair{"continue"sv, TokenType::Continue},
	std::pair{"return"sv, TokenType::Return},
	std::pair{"throw"sv, TokenType::Throw},
};

constexpr auto known_symbols = std::array{
	std::pair{"="sv, TokenType::Equals},
	std::pair{"@"sv, TokenType::At},
	std::pair{"."sv, TokenType::Dot},
	std::pair{","sv, TokenType::Comma},
	std::pair{"{"sv, TokenType::LBrace},
	std::pair{"}"sv, TokenType::RBrace},
	std::pair{";"sv, TokenType::Semicolon},
	std::pair{"("sv, TokenType::LParen},
	std::pair{")"sv, TokenType::RParen},
	std::pair{"&&"sv, TokenType::And},
	std::pair{"||"sv, TokenType::Or},

	std::pair{"+"sv,  TokenType::Operator},
	std::pair{"-"sv,  TokenType::Operator},
	std::pair{"*"sv,  TokenType::Operator},
	std::pair{"/"sv,  TokenType::Operator},
	std::pair{"%"sv,  TokenType::Operator},
	std::pair{"!"sv,  TokenType::Operator},
	std::pair{"<"sv,  TokenType::Operator},
	std::pair{"<="sv, TokenType::Operator},
	std::pair{">"sv,  TokenType::Operator},
	std::pair{">="sv, TokenType::Operator},
	std::pair{"=="sv, TokenType::Operator},
	std::pair{"!="sv, TokenType::Operator},
};

constexpr auto known_ident_specials = std::array{'_', '?'};

class Tokenizer {
private:
	std::string_view input;
	size_t position;
public:

	Tokenizer(std::string_view);

	Token peek();
	Token next();

private:
	std::string_view remaining();
	char following(size_t i);
};

Tokenizer::Tokenizer(std::string_view input) : input(input), position(0) {}

bool starts_with(const std::string_view& str, const std::string_view& prefix) {
	if (prefix.size() > str.size()) {
		return false;
	}
	size_t n = prefix.size();
	for (size_t i = 0; i < n; ++i) {
		if (str[i] != prefix[i]) {
			return false;
		}
	}
	return true;
}

bool is_ident_special(char c) {
	return std::count(known_ident_specials.begin(), known_ident_specials.end(), c) > 0;
}

Token Tokenizer::peek() {
	// whitespace and comments
	while (true) {
		while (std::isspace(following(0))) {
			++position;
		}
		if (!starts_with(remaining(), "//")) {
			break;
		}
		else {
			position += 2;
			while (following(0) != '\0' && following(0) != '\n') {
				++position;
			}
		}
	}
	if (position >= input.size()) {
		return Token{TokenType::Eof, ""};
	}
	// symbol
	{
		std::pair<std::string_view, TokenType> max;
		for (auto& sym : known_symbols) {
			if (starts_with(remaining(), sym.first) && sym.first > max.first) {
				max = sym;
			}
		}
		if (max.first.size() > 0) {
			return Token{max.second, max.first};
		}
	}
	// int literal
	if (std::isdigit(following(0))) {
		size_t n = 1;
		while (std::isdigit(following(n))) {
			++n;
		}
		return Token{TokenType::Int, remaining().substr(0, n)};
	}
	// string literal
	if (following(0) == '\"') {
		size_t n = 1;
		while (n < input.size()) {
			if (following(n) == '"') {
				return Token{TokenType::String, remaining().substr(0, n+1)};
			}
			n += (following(n) == '\\' && following(n+1) == '"') ? 2 : 1;
		}
	}
	// identifier
	if (std::isalpha(following(0)) || is_ident_special(following(0))) {
		size_t n = 1;
		while (std::isalnum(following(n)) || is_ident_special(following(n))) {
			++n;
		}
		auto ident = remaining().substr(0, n);
		for (auto& kw : known_keywords) {
			if (ident == kw.first) {
				return Token{kw.second, ident};
			}
		}
		return Token{TokenType::Identifier, ident};
	}
	return Token{TokenType::Unknown, remaining().substr(0, 1)};
}

Token Tokenizer::next() {
	auto tok = peek();
	if (tok.type != TokenType::Eof && tok.type != TokenType::Unknown) {
		position += tok.str.size();
	}
	return tok;
}

std::string_view Tokenizer::remaining() {
	return {input.data() + position, input.size() - position};
}

char Tokenizer::following(size_t i) {
	return (position + i < input.size()) ? input[position + i] : '\0';
}

// parser itself

class Parser {
private:
	Tokenizer tokens;

public:
	Parser(std::string_view input);

	ExpressionPtr parse_string();
	ExpressionPtr parse_int();

	ExpressionPtr parse_variable_or_assign();
	ExpressionPtr parse_let();

	std::vector<ExpressionPtr> parse_block();
	ExpressionPtr parse_if();
	ExpressionPtr parse_while();
	ExpressionPtr parse_try();

	std::vector<std::string> parse_arguments();
	ExpressionPtr parse_lambda();
	ExpressionPtr parse_method();

	ExpressionPtr parse_return();
	ExpressionPtr parse_throw();

	ExpressionPtr parse_basic_expr();
	ExpressionPtr parse_unary_expr();
	ExpressionPtr parse_expr();
	ExpressionSeq parse_expr_seq();
};

Parser::Parser(std::string_view input) : tokens(input) {}

template<typename... Args>
[[noreturn]] void error(Args&&... args) {
	std::stringstream buf;
	(buf << ... << std::forward<Args>(args));
	throw std::runtime_error(buf.str());
}

template<typename E, typename C>
[[noreturn]] void unexpected(const Token& tok, E&& expected, C&& context) {
	error("Unexpected ", tok, " in ",
			std::forward<C>(context), ", expecting ", std::forward<E>(expected));
}

template<typename C>
void expect(const Token& tok, TokenType typ, C&& context) {
	if (tok.type != typ) {
		unexpected(tok, typ, std::forward<C>(context));
	}
}

ExpressionPtr Parser::parse_string() {
	auto tok = tokens.next();
	expect(tok, TokenType::String, "string literal");
	if (auto str = unquote_string(tok.str)) {
		return make_expr<StringExpr>(*str);
	}
	else {
		error("Invalid string literal ", tok.str);
	}
}

ExpressionPtr Parser::parse_int() {
	auto tok = tokens.next();
	expect(tok, TokenType::Int, "int literal");
	try {
		int64_t n = std::stol(std::string(tok.str));
		return make_expr<IntExpr>(n);
	}
	catch (std::exception& ex) {
		error("Invalid integer literal ", tok.str);
	}
}

ExpressionPtr Parser::parse_variable_or_assign() {
	auto tok = tokens.next();
	expect(tok, TokenType::Identifier, "variable");
	auto name = std::string(tok.str);
	if (tokens.peek().type == TokenType::Equals) {
		tokens.next();
		auto value = parse_expr();
		return make_expr<AssignExpr>(name, value);
	}
	else {
		return make_expr<VariableExpr>(name);
	}
}

ExpressionPtr Parser::parse_let() {
	expect(tokens.next(), TokenType::Let, "let binding");
	auto tok = tokens.next();
	expect(tok, TokenType::Identifier, "let binding");
	auto name = std::string(tok.str);
	expect(tokens.next(), TokenType::Equals, "let binding");
	auto value = parse_expr();
	return make_expr<LetExpr>(name, value);
}

std::vector<ExpressionPtr> Parser::parse_block() {
	expect(tokens.next(), TokenType::LBrace, "block");
	std::vector<ExpressionPtr> result;
	while (true) {
		auto tok = tokens.peek();
		if (tok.type != TokenType::RBrace && tok.type != TokenType::Semicolon) {
			result.emplace_back(parse_expr());
		}
		else {
			result.emplace_back(make_expr<EmptyExpr>());
		}
		tok = tokens.next();
		if (tok.type == TokenType::RBrace) {
			break;
		}
		else if (tok.type != TokenType::Semicolon) {
			unexpected(tok, "right brace or semicolon", "block");
		}
	}
	return result;
}

ExpressionPtr Parser::parse_if() {
	expect(tokens.next(), TokenType::If, "if expression");
	IfExpr result;
	auto pred = parse_expr();
	auto body = parse_block();
	result.branches.emplace_back(pred, body);
	while (tokens.peek().type == TokenType::Else && !result.otherwise) {
		tokens.next();
		if (tokens.peek().type == TokenType::If) {
			tokens.next();
			pred = parse_expr();
			body = parse_block();
			result.branches.emplace_back(pred, body);
		}
		else {
			body = parse_block();
			result.otherwise = body;
		}
	}
	return make_expr(result);
}

ExpressionPtr Parser::parse_while() {
	expect(tokens.next(), TokenType::While, "while loop");
	auto cond = parse_expr();
	auto body = parse_block();
	return make_expr<WhileExpr>(cond, body);
}

ExpressionPtr Parser::parse_try() {
	expect(tokens.next(), TokenType::Try, "try-catch expression");
	auto body = parse_block();
	expect(tokens.next(), TokenType::Catch, "try-catch expression");
	auto tok = tokens.next();
	expect(tok, TokenType::Identifier, "try-catch expression");
	auto error = std::string(tok.str);
	auto handler = parse_block();
	return make_expr<TryExpr>(body, error, handler);
}

std::vector<std::string> Parser::parse_arguments() {
	expect(tokens.next(), TokenType::LParen, "argument list");
	std::vector<std::string> result;
	while (tokens.peek().type != TokenType::RParen) {
		auto tok = tokens.next();
		expect(tok, TokenType::Identifier, "argument list");
		result.emplace_back(tok.str);
		tok = tokens.peek();
		if (tok.type == TokenType::Comma) {
			tokens.next();
		}
		else {
			expect(tok, TokenType::RParen, "argument list");
		}
	}
	tokens.next();
	return result;
}

ExpressionPtr Parser::parse_lambda() {
	expect(tokens.next(), TokenType::Fn, "lambda");
	auto args = parse_arguments();
	auto body = parse_block();
	return make_expr<LambdaExpr>(args, body);
}

ExpressionPtr Parser::parse_method() {
	expect(tokens.next(), TokenType::Method, "method");
	MethodExpr result;
	auto tok = tokens.peek();
	if (tok.type == TokenType::LParen) {
		result.args = parse_arguments();
		result.body = parse_block();
	}
	else if (tok.type == TokenType::LBrace) {
		result.body = parse_block();
	}
	else {
		unexpected(tok, "arguments or a block", "method");
	}
	return make_expr(result);
}

ExpressionPtr Parser::parse_return() {
	expect(tokens.next(), TokenType::Return, "return");
	auto tok = tokens.peek();
	try {
		auto value = parse_expr();
		return make_expr<ReturnExpr>(value);
	}
	catch (std::runtime_error err) {
		// If parse_expr failed without consuming any tokens, we are dealing
		// with an empty return.
		if (tokens.peek().str.data() == tok.str.data()) {
			return make_expr<ReturnExpr>(std::nullopt);
		}
		else {
			throw err;
		}
	}
}

ExpressionPtr Parser::parse_throw() {
	expect(tokens.next(), TokenType::Throw, "throw");
	auto value = parse_expr();
	return make_expr<ThrowExpr>(value);
}

ExpressionPtr Parser::parse_basic_expr() {
	auto tok = tokens.peek();
	ExpressionPtr result;
	switch (tok.type) {
	case TokenType::String:
		result = parse_string();
		break;
	case TokenType::Int:
		result = parse_int();
		break;
	case TokenType::Identifier:
		result = parse_variable_or_assign();
		break;

	case TokenType::Let:
		result = parse_let();
		break;
	case TokenType::If:
		result = parse_if();
		break;
	case TokenType::While:
		result = parse_while();
		break;
	case TokenType::Try:
		result = parse_try();
		break;
	case TokenType::Fn:
		result = parse_lambda();
		break;
	case TokenType::Method:
		result = parse_method();
		break;
	case TokenType::Break:
		tokens.next();
		result = make_expr<BreakExpr>();
		break;
	case TokenType::Continue:
		tokens.next();
		result = make_expr<ContinueExpr>();
		break;
	case TokenType::Return:
		result = parse_return();
		break;
	case TokenType::Throw:
		result = parse_throw();
		break;

	case TokenType::LBrace:
		result = make_expr<BlockExpr>(parse_block());
		break;
	case TokenType::LParen:
		tokens.next();
		if (tokens.peek().type == TokenType::RParen) {
			result = make_expr<EmptyExpr>();
		}
		else {
			result = parse_expr();
		}
		expect(tokens.next(), TokenType::RParen, "parenthesised expression");
		break;

	default:
		error("Unexpected ", tok, ", expecting an expression");
	}
	return result;
}

ExpressionPtr Parser::parse_unary_expr() {
	auto tok = tokens.peek();
	if (tok.type == TokenType::Operator) {
		tokens.next();
		return make_expr<UnaryExpr>(std::string(tok.str), parse_unary_expr());
	}
	auto result = parse_basic_expr();
	while (true) {
		tok = tokens.peek();
		// function call
		if (tok.type == TokenType::LParen) {
			tokens.next();
			std::vector<ExpressionPtr> args;
			while (tokens.peek().type != TokenType::RParen) {
				args.emplace_back(parse_expr());
				tok = tokens.peek();
				if (tok.type == TokenType::Comma) {
					tokens.next();
				}
				else {
					expect(tok, TokenType::RParen, "function call");
				}
			}
			tokens.next();
			result = make_expr<CallExpr>(result, args);
		}
		// message send
		else if (tok.type == TokenType::Dot) {
			tokens.next();
			tok = tokens.next();
			if (tok.type != TokenType::Identifier && tok.type != TokenType::Operator) {
				error("Unexpected ", tok, " expecting identifier or operator");
			}
			result = make_expr<SendExpr>(result, std::string(tok.str));
		}
		// property access
		else if (tok.type == TokenType::At) {
			tokens.next();
			tok = tokens.next();
			expect(tok, TokenType::Identifier, "property access");
			auto prop = std::string(tok.str);
			if (tokens.peek().type == TokenType::Equals) {
				tokens.next();
				auto value = parse_expr();
				result = make_expr<SetPropExpr>(result, prop, value);
			}
			else {
				result = make_expr<GetPropExpr>(result, prop);
			}
		}
		else {
			break;
		}
	}
	return result;
}

ExpressionPtr Parser::parse_expr() {
	auto result = parse_unary_expr();
	while (tokens.peek().type == TokenType::Operator ||
			tokens.peek().type == TokenType::And ||
			tokens.peek().type == TokenType::Or)
	{
		auto tok = tokens.next();
		auto rhs = parse_unary_expr();
		switch (tok.type) {
		case TokenType::Operator:
			result = make_expr<BinaryExpr>(std::string(tok.str), result, rhs);
			break;
		case TokenType::And:
			result = make_expr<AndExpr>(result, rhs);
			break;
		case TokenType::Or:
			result = make_expr<OrExpr>(result, rhs);
			break;
		default:
			break;
		}
	}
	return result;
}

ExpressionSeq Parser::parse_expr_seq() {
	ExpressionSeq result;
	while (true) {
		auto tok = tokens.peek();
		if (tok.type != TokenType::Semicolon && tok.type != TokenType::Eof) {
			result.exprs.emplace_back(parse_expr());
		}
		else {
			result.exprs.emplace_back(make_expr<EmptyExpr>());
		}
		tok = tokens.next();
		if (tok.type == TokenType::Eof) {
			break;
		}
		else if (tok.type != TokenType::Semicolon) {
			error("Unexpected ", tok, ", expecting semicolon or end of file");
		}
	}
	return result;
}

}  // namespace anonymous

ExpressionSeq parse_expr_seq(const std::string_view& input) {
	auto parser = Parser(input);
	return parser.parse_expr_seq();
}
