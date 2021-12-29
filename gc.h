#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace detail { class BoxBase; }
template<typename> class Ptr;

// Visitor used for tracing objects.
class Tracer {
	friend class Collector;

private:
	std::function<void(detail::BoxBase*)> callback;

	explicit Tracer(std::function<void(detail::BoxBase*)>);

public:
	template<typename T>
	void visit(const Ptr<T>& ptr);
};

// Trait defining the tracing function. All types used with the gc must implement it.
template<typename T>
struct Traceable {
	// Must be set to true for all specializations.
	static const bool enabled = false;

	// Visits all accesible Ptr<T> values with supplied tracer.
	static void trace(const T&, Tracer&) = delete;
};

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

	// Calls Traceable::trace on the contained value.
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
	friend class Tracer;
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

template<typename T>
void Tracer::visit(const Ptr<T>& ptr) {
	callback(ptr.box);
}

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
	Traceable<T>::trace(*reinterpret_cast<T*>(value), t);
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
		if (static_cast<void*>(x) != static_cast<void*>(y)) {
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
	if (x != nullptr && y == nullptr) {  // dynamic cast failed
		return std::nullopt;
	}
	if (static_cast<void*>(x) != static_cast<void*>(y)) {  // address changed
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
	Traceable<T>::trace(inner, t);
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
	static_assert(Traceable<T>::enabled,
			"Objects managed by the gc need to implement Traceable");
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
	static_assert(Traceable<T>::enabled,
			"Rooted objects need to implement Traceable");
	return Root<T>(&root_head, std::forward<Args>(args)...);
}

// Traceable implementations for builtin types.

template<typename T>
struct Traceable<Ptr<T>> {
	static const bool enabled = true;

	static void trace(const Ptr<T>& ptr, Tracer& t) {
		t.visit(ptr);
	}
};

template<>
struct Traceable<std::string> {
	static const bool enabled = true;

	static void trace(const std::string&, Tracer&) {}
};

template<typename T>
struct Traceable<std::vector<T>> {
	static_assert(Traceable<T>::enabled, "vector elements must implement Traceable");

	static const bool enabled = true;

	static void trace(const std::vector<T>& xs, Tracer& t) {
		for (const auto& x : xs) {
			Traceable<T>::trace(x, t);
		}
	}
};
