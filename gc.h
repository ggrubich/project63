#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace detail { class BoxBase; }
template<typename> class Ptr;

// Visitor used for tracing objects.
using Tracer = std::function<void(const Ptr<const void>&)>;

// Trait defining the tracing function, required for allocating and rooting
// objects with the GC.
// Type T implements Trace when specialization Trace<T> is an object with
// a default constructor and an operator() with a matching signature.
// Formally, those requirements are described by is_traceable_v predicate.
template<typename T>
struct Trace {
	// Visits all directly accesible Ptr<T> values with the supplied tracer.
	void operator()(const T&, Tracer&) = delete;
};

// Checks if type T implements Trace.
template<typename T>
constexpr bool is_traceable_v =
	std::is_default_constructible_v<Trace<T>> &&
	std::is_invocable_v<Trace<T>, const T&, Tracer&>;

template<typename T>
using is_traceable = std::bool_constant<is_traceable_v<T>>;

namespace detail {

// Base class for internal nodes used by the garbage collector.
struct BoxBase {
	// True if contanied value was not destroyed yet.
	bool valid;
	// Switches to true after being visited in the mark phase.
	bool marked;
	// Byte offset betweet start of the struct and the contained value.
	uint8_t offset;
	// Number of existing weak pointers.
	uint64_t ptrs;
	// Linked list of all boxes.
	BoxBase* next;

	BoxBase(uint8_t offset, BoxBase* next);
	virtual ~BoxBase() = default;

	// Calls Trace on the contained value.
	virtual void trace(Tracer&) = 0;
	// Calls the destructor on the contained value.
	virtual void destroy() = 0;
};

// Box implementation for a specific type.
template<typename T>
struct Box : public BoxBase {
	// Contained value managed by the gc.
	alignas(T) uint8_t value[sizeof(T)];

	template<typename... Args>
	Box(BoxBase* next, Args&&... args);

	void trace(Tracer& t) override;
	void destroy() override;
};

}  // namespace detail

// Pointer managed by the gc. This is a *weak* pointer - triggering a gc cycle
// can invalidate it at any time. To prevent that from happening, currently used
// pointers must be rooted (see class Root).
template<typename T>
class Ptr {
	template<typename> friend class Ptr;
	friend class Collector;

private:
	detail::BoxBase* box;

	explicit Ptr(detail::BoxBase* box);

public:
	Ptr();
	~Ptr();
	Ptr(const Ptr<T>&);
	Ptr(Ptr<T>&&);
	Ptr& operator=(Ptr<T>);

	// Implicit pointer conversion.
	// For implementation reasons, conversions that would change pointer's
	// address will throw a std::bad_cast (that can happen when casting a class
	// to its non-first base class).
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	Ptr(const Ptr<U>&);

	// Performs a pointer cast without any checks. This can be seen as
	// an equivalent to C-style casts.
	template<typename U>
	Ptr<U> cast() const;

	// Performs a pointer cast with runtime checks.
	// If running dynamic_cast<U*> on the contained pointer would succeed without
	// changing its address, dyncast returns Ptr<U>, otherwise nullopt is returned.
	template<typename U>
	std::optional<Ptr<U>> dyncast() const;

	// Checks if the pointer is valid.
	bool valid() const;

	// Accesses the contained data.
	// Throws std::runtime_error when trying to access an invalid pointer.
	T* address() const;
	std::add_lvalue_reference_t<T> get() const;
	std::add_lvalue_reference_t<T> operator*() const;
	T* operator->() const;
};

namespace detail {

struct RootBase {
	RootBase** head;
	RootBase* prev;
	RootBase* next;

	explicit RootBase(RootBase** head);
	~RootBase();
	RootBase(const RootBase&);
	RootBase& operator=(const RootBase&);

	void attach(RootBase** head);
	void detach();

	virtual void trace(Tracer&) = 0;
};

}  // namespace detail

// RAII guard representing a rooted value. Existence of a root guarantees
// that pointers reachable from it will not be invalidated for the duration
// of root's lifetime.
// Contained data can be accessed by dereferencing the root.
//
// Generally, roots should be used on the stack - in local or global variables.
// An object managed by gc should not contain references to roots. In particular,
// one should avoid creating root cycles, as those can cause memory leaks.
template<typename T>
class Root : private detail::RootBase {
	friend class Collector;
	template<typename> friend class Root;

private:
	template<typename... Args>
	explicit Root(detail::RootBase** head, Args&&... args);

	void trace(Tracer& t) override;

public:
	// The rooted value itself.
	T inner;

	// Converting constructors from other roots.
	template<typename U>
	Root(const Root<U>&);
	template<typename U>
	Root(Root<U>&&);

	// Value assignemnt.
	Root<T>& operator=(const T&);
	Root<T>& operator=(T&&);

	// Pointer-like operations on the contained value.
	const T& operator*() const& noexcept;
	T& operator*() & noexcept;
	const T&& operator*() const && noexcept;
	T&& operator*() && noexcept;

	const T* operator->() const noexcept;
	T* operator->() noexcept;
};

// The garbage collector object.
class Collector {
private:
	detail::BoxBase* box_head;
	detail::RootBase* root_head;
	size_t allocations;
	size_t treshold;

public:
	Collector();
	~Collector();

	Collector(Collector&&) = delete;
	Collector& operator=(Collector&&) = delete;

	// Allocates a new gc managed pointer.
	template<typename T, typename... Args>
	Root<Ptr<T>> alloc(Args&&... args);

	// Roots a value.
	template<typename T>
	Root<std::decay_t<T>> root(T&& value);

	template<typename T, typename... Args>
	Root<T> root(std::in_place_t, Args&&... args);

	// Triggers a gc cycle.
	void collect();
};

// Implementations

namespace detail {

template<typename T>
template<typename... Args>
Box<T>::Box(BoxBase* next, Args&&... args)
	: BoxBase(
			reinterpret_cast<uintptr_t>(value) - reinterpret_cast<uintptr_t>(this),
			next)
{
	new(value) T(std::forward<Args>(args)...);
}

template<typename T>
void Box<T>::trace(Tracer& t) {
	Trace<T>{}(*reinterpret_cast<T*>(value), t);
}

template<typename T>
void Box<T>::destroy() {
	reinterpret_cast<T*>(value)->~T();
	std::memset(value, 0, sizeof(T));
}

}  // namespace detail

template<typename T>
Ptr<T>::Ptr(detail::BoxBase* box) : box(box) {
	if (box != nullptr) {
		box->ptrs += 1;
	}
}

template<typename T>
Ptr<T>::Ptr() : box(nullptr) {}

template<typename T>
Ptr<T>::~Ptr() {
	if (box != nullptr) {
		box->ptrs -= 1;
	}
}

template<typename T>
Ptr<T>::Ptr(const Ptr<T>& ptr) : Ptr(ptr.box) {}

template<typename T>
Ptr<T>::Ptr(Ptr<T>&& ptr) : Ptr() {
	std::swap(box, ptr.box);
}

template<typename T>
Ptr<T>& Ptr<T>::operator=(Ptr<T> ptr) {
	std::swap(box, ptr.box);
	return *this;
}

template<typename T>
template<typename U, typename>
Ptr<T>::Ptr(const Ptr<U>& ptr) : Ptr(ptr.box) {
	if (valid()) {
		U* x = ptr.address();
		T* y = x;
		if (reinterpret_cast<uintptr_t>(x) != reinterpret_cast<uintptr_t>(y)) {
			throw std::bad_cast();
		}
	}
}

template<typename T>
template<typename U>
Ptr<U> Ptr<T>::cast() const {
	return Ptr<U>(box);
}

template<typename T>
template<typename U>
std::optional<Ptr<U>> Ptr<T>::dyncast() const {
	T* x = valid() ? address() : nullptr;
	U* y = dynamic_cast<U*>(x);
	// dynamic cast failed
	if (x != nullptr && y == nullptr) {
		return std::nullopt;
	}
	// address changed
	if (reinterpret_cast<uintptr_t>(x) != reinterpret_cast<uintptr_t>(y)) {
		return std::nullopt;
	}
	return Ptr<U>(box);
}

template<typename T>
bool Ptr<T>::valid() const {
	return box != nullptr && box->valid;
}

template<typename T>
T* Ptr<T>::address() const {
	if (!valid()) {
		throw std::runtime_error("can't access an invalid Ptr");
	}
	uint8_t* p = reinterpret_cast<uint8_t*>(box) + box->offset;
	return reinterpret_cast<T*>(p);
}

template<typename T>
std::add_lvalue_reference_t<T> Ptr<T>::get() const {
	return *address();
}

template<typename T>
std::add_lvalue_reference_t<T> Ptr<T>::operator*() const {
	return get();
}

template<typename T>
T* Ptr<T>::operator->() const {
	return address();
}

template<typename T>
template<typename... Args>
Root<T>::Root(detail::RootBase** head, Args&&... args)
	: detail::RootBase(head)
	, inner(std::forward<Args>(args)...)
{}

template<typename T>
void Root<T>::trace(Tracer& t) {
	Trace<T>{}(inner, t);
}

template<typename T>
template<typename U>
Root<T>::Root(const Root<U>& other)
	: Root(other.head, other.inner)
{}

template<typename T>
template<typename U>
Root<T>::Root(Root<U>&& other)
	: Root(other.head, std::move(other.inner))
{}

template<typename T>
Root<T>& Root<T>::operator=(const T& x) {
	inner = x;
	return *this;
}

template<typename T>
Root<T>& Root<T>::operator=(T&& x) {
	inner = std::move(x);
	return *this;
}

template<typename T> const T& Root<T>::operator*() const& noexcept { return inner; }
template<typename T> T& Root<T>::operator*() & noexcept { return inner; }
template<typename T> const T&& Root<T>::operator*() const&& noexcept { return std::move(inner); }
template<typename T> T&& Root<T>::operator*() && noexcept { return std::move(inner); }

template<typename T> const T* Root<T>::operator->() const noexcept { return &inner; }
template<typename T> T* Root<T>::operator->() noexcept { return &inner; }

template<typename T, typename... Args>
Root<Ptr<T>> Collector::alloc(Args&&... args) {
	static_assert(is_traceable_v<T>,
			"Objects managed by the gc need to implement Trace");
	if (allocations >= treshold) {
		collect();
		treshold = std::max(allocations * 2, size_t(128));
	}
	auto box = new detail::Box<T>(box_head, std::forward<Args>(args)...);
	box_head = box;
	allocations += 1;
	return root(Ptr<T>(box));
}

template<typename T>
Root<std::decay_t<T>> Collector::root(T&& value) {
	return root<std::decay_t<T>>(std::in_place, std::forward<T>(value));
}

template<typename T, typename... Args>
Root<T> Collector::root(std::in_place_t, Args&&... args) {
	static_assert(is_traceable_v<T>,
			"Rooted objects need to implement Trace");
	return Root<T>(&root_head, std::forward<Args>(args)...);
}

// Trace implementations for builtin types.

template<typename T>
struct Trace<Ptr<T>> {
	void operator()(const Ptr<T>& x, Tracer& t) { t(x); }
};

#define X(T) \
	template<> struct Trace<T> { \
		void operator()(const T&, Tracer&) {} \
	};

X(bool) X(char)
X(uint8_t) X(uint16_t) X(uint32_t) X(uint64_t)
X(int8_t)  X(int16_t)  X(int32_t)  X(int64_t)
X(float) X(double) X(long double)
X(std::string)

#undef X

template<typename T>
struct Trace<T*> {
	template<typename Q = std::remove_const_t<T>,
		typename = std::enable_if_t<is_traceable_v<Q>>
	>
	void operator()(T* x, Tracer& t) {
		if (x != nullptr) {
			Trace<Q>{}(std::as_const(*x), t);
		}
	}
};

template<typename T>
struct Trace<std::vector<T>> {
	template<typename Q = T, typename = std::enable_if_t<is_traceable_v<Q>>>
	void operator()(const std::vector<T>& xs, Tracer& t) {
		for (const auto& x : xs) {
			Trace<T>{}(x, t);
		}
	}
};

template<typename T>
struct Trace<std::optional<T>> {
	template<typename Q = T, typename = std::enable_if_t<is_traceable_v<Q>>>
	void operator()(const std::optional<T>& x, Tracer& t) {
		if (x) {
			Trace<T>{}(*x, t);
		}
	}
};

template<typename K, typename V>
struct Trace<std::unordered_map<K, V>> {
	template<typename K1 = K, typename V1 = V,
		typename = std::enable_if_t<is_traceable_v<K1> && is_traceable_v<V1>>
	>
	void operator()(const std::unordered_map<K, V>& map, Tracer& t) {
		for (const auto& x : map) {
			Trace<K>{}(x.first, t);
			Trace<V>{}(x.second, t);
		}
	}
};

namespace detail {

template<typename... Ts>
struct TraceVariant {
	void operator()(const std::variant<Ts...>& v, Tracer& t) {
		std::visit([&](const auto& x) {
			using T = std::decay_t<decltype(x)>;
			Trace<T>{}(x, t);
		}, v);
	}
};

}  // namespace detail

template<typename... Ts>
struct Trace<std::variant<Ts...>>
	: std::conditional_t<
		std::conjunction_v<is_traceable<Ts>...>,
		detail::TraceVariant<Ts...>,
		std::monostate
	>
{};
