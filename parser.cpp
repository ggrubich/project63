#include "parser.h"

#include "strings.h"

#include <algorithm>

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
				s << (indent+1) << "],\n";
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
			s << indent << "Return{\n";
			show_expr(s, indent+1, *expr.value) << "\n";
			s << indent << "}";
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
		[](const ReturnExpr& x, const ReturnExpr& y) { return *x.value == *y.value; },
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
