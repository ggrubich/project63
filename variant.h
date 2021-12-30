#pragma once

#include "gc.h"

#include <type_traits>
#include <variant>

// Helper type for writing visitors.
template<typename... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template<typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

// Inheritable helper type for declaring sum types.
//
// Motivation:
// We want to extend std::variant with custom methods. The most obvious solution
// would be subclassing std::variant, but unfortunately such subclasses don't
// work with std::visit and other stdlib functions.
// To solve this problem, we can declare a wrapper type which stores a std::variant
// as its member variable and declare new methods on the wrapper itself.
// `Variant` is a base class for those kinds of wrappers which also contains
// some convenient methods for accessing the their inner value.
template<typename... Ts>
struct Variant {
	using Inner = std::variant<Ts...>;

	Inner inner;

	// Constructs the variant from the inner type.
	template<typename T,
		typename = std::enable_if_t<
			std::is_convertible_v<T, Inner> &&
			!std::is_base_of_v<Variant, std::remove_reference_t<T>>
		>
	>
	Variant(T&& t) : inner(std::forward<T>(t)) {}

	// Applies the visitor V to the variant.
	template<typename V> decltype(auto) visit(V&& v) const& {
		return std::visit(std::forward<V>(v), inner);
	}
	template<typename V> decltype(auto) visit(V&& v) & {
		return std::visit(std::forward<V>(v), inner);
	}
	template<typename V> decltype(auto) visit(V&& v) const&& {
		return std::visit(std::forward<V>(v), std::move(inner));
	}
	template<typename V> decltype(auto) visit(V&& v) && {
		return std::visit(std::forward<V>(v), std::move(inner));
	}

	// Tries to retrieve type T from the variant, on failure throws std::bad_variant_access.
	template<typename T> const T& get() const& {
		return std::get<T>(inner);
	}
	template<typename T> T& get() & {
		return std::get<T>(inner);
	}
	template<typename T> const T&& get() const&& {
		return std::get<T>(std::move(inner));
	}
	template<typename T> T&& get() && {
		return std::get<T>(std::move(inner));
	}

	// Checks if the variant contains the given type T.
	template<typename T> bool holds() const {
		return std::holds_alternative<T>(inner);
	}
};

template<typename... Ts>
struct Trace<Variant<Ts...>> {
	template<
		typename V = std::variant<Ts...>,
		typename = std::enable_if_t<is_traceable_v<V>>
	>
	void operator()(const Variant<Ts...>& v, Tracer& t) {
		Trace<V>{}(v.inner, t);
	}
};
