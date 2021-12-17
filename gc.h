#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <utility>

class Tracer;
template<typename T> class Ptr;

namespace detail {

// Base class for internal nodes used by the garbage collector.
struct Box {
	// True if contanied value was not destroyed yet.
	bool valid;
	// Switches to true after being visited in the mark phase.
	bool marked;
	// Number of existing weak pointers.
	uint64_t ptrs;
	// Linked list of all boxes.
	Box* next;

	virtual ~Box() = default;
	// Calls Traceable::trace on the contained value.
	virtual void trace(Tracer&) = 0;
	// Calls the destructor on the contained value.
	virtual void destroy() = 0;
};

// Box implementation for a specific type.
template<typename T>
struct TBox : public Box {
	// Contained value managed by the gc.
	alignas(T) uint8_t value[sizeof(T)];

	void trace(Tracer& t) override;
	void destroy() override;
};

}  // namespace detail

// Visitor used for tracing objects.
class Tracer {
	friend class Collector;

private:
	std::function<void(detail::Box*)> callback;

	explicit Tracer(std::function<void(detail::Box*)>);

public:
	template<typename T>
	void visit(Ptr<T> ptr);
};

// Trait defining the tracing function. All types used with the gc must implement it.
template<typename T>
struct Traceable {
	// Must be set to true for all specializations.
	static const bool enabled = false;

	// Visits all accesible Ptr<T> values with supplied tracer.
	static void trace(const T&, Tracer&) = delete;
};

// Pointer managed by the gc. This is a *weak* pointer - triggering a gc cycle
// can invalidate it at any time. To prevent that from happening, currently used
// pointers must be rooted (see class Root). For this reason Ptr does not
// implement any methods for accessing the contained data - that must be done
// through a Root.
template<typename T>
class Ptr {
	friend class Tracer;
	friend class Collector;

private:
	detail::TBox<T>* box;

	explicit Ptr(detail::TBox<T>* box);

public:
	Ptr();
	~Ptr();
	Ptr(const Ptr<T>&);
	Ptr(Ptr<T>&&);
	Ptr& operator=(Ptr<T>);

	// Checks if the pointer is valid.
	bool valid() const;

	// Accesses the contained data.
	T& operator*() const;
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
template<typename T, bool Final = true>
class Root : protected detail::RootBase {
	friend class Collector;
	template<typename, bool> friend class Root;

protected:
	template<typename... Args>
	explicit Root(detail::RootBase** head ,Args&&... args);

	void trace(Tracer& t) override;

public:
	// The rooted value itself.
	T value;

	// Converting constructors from other roots.
	template<typename U>
	Root(const Root<U>&);
	template<typename U>
	Root(Root<U>&&);

	// Value assignemnt.
	Root<T>& operator=(const T&);
	Root<T>& operator=(T&&);

	// Implicit conversions to the contained type.
	operator T&() & noexcept;
	operator const T&() const& noexcept;
	operator T&&() && noexcept;
	operator const T&&() const&& noexcept;

	// Pointer-like operations on the contained value.
	T& operator*() & noexcept;
	const T& operator*() const& noexcept;
	T&& operator*() && noexcept;
	const T&& operator*() const&& noexcept;
	T* operator->() noexcept;
	const T* operator->() const noexcept;
};

// Specialization of Root for pointers, mainly for a more convenient interface.
template<typename T>
class Root<Ptr<T>> : public Root<Ptr<T>, false> {
private:
	using Base = Root<Ptr<T>, false>;
	using Base::Root;

public:
	using Base::value;
	using Base::operator=;

	T& operator*() const;
	T* operator->() const;
};

// The garbage collector object.
class Collector {
private:
	detail::Box* box_head;
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
void TBox<T>::trace(Tracer& t) {
	Traceable<T>::trace(*reinterpret_cast<T*>(value), t);
}

template<typename T>
void TBox<T>::destroy() {
	reinterpret_cast<T*>(value)->~T();
	std::memset(value, 0, sizeof(T));
}

}  // namespace detail

template<typename T>
void Tracer::visit(Ptr<T> ptr) {
	callback(ptr.box);
}

template<typename T>
Ptr<T>::Ptr(detail::TBox<T>* box) : box(box) {
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
bool Ptr<T>::valid() const {
	return box != nullptr && box->valid;
}

template<typename T>
T& Ptr<T>::operator*() const {
	if (!valid()) {
		throw std::runtime_error("can't root invalid Ptr");
	}
	return *reinterpret_cast<T*>(box->value);
}

template<typename T>
T* Ptr<T>::operator->() const {
	return &**this;
}

template<typename T, bool F>
template<typename... Args>
Root<T, F>::Root(detail::RootBase** head, Args&&... args)
	: detail::RootBase(head)
	, value(std::forward<Args>(args)...)
{}

template<typename T, bool F>
void Root<T, F>::trace(Tracer& t) {
	Traceable<T>::trace(value, t);
}

template<typename T, bool F>
template<typename U>
Root<T, F>::Root(const Root<U>& other)
	: Root(other.head, other.value)
{}

template<typename T, bool F>
template<typename U>
Root<T, F>::Root(Root<U>&& other)
	: Root(other.head, std::move(other.value))
{}

template<typename T, bool F>
Root<T>& Root<T, F>::operator=(const T& x) {
	value = x;
	return *static_cast<Root<T>*>(this);
}

template<typename T, bool F>
Root<T>& Root<T, F>::operator=(T&& x) {
	value = std::move(x);
	return *static_cast<Root<T>*>(this);
}

template<typename T, bool F>
Root<T, F>::operator T&() & noexcept { return value; }
template<typename T, bool F>
Root<T, F>::operator const T&() const& noexcept { return value; }
template<typename T, bool F>
Root<T, F>::operator T&&() && noexcept { return std::move(value); }
template<typename T, bool F>
Root<T, F>::operator const T&&() const&& noexcept { return std::move(value); }

template<typename T, bool F>
T& Root<T, F>::operator*() & noexcept { return value; }
template<typename T, bool F>
const T& Root<T, F>::operator*() const& noexcept { return value; }
template<typename T, bool F>
T&& Root<T, F>::operator*() && noexcept { return std::move(value); }
template<typename T, bool F>
const T&& Root<T, F>::operator*() const&& noexcept { return std::move(value); }
template<typename T, bool F>
T* Root<T, F>::operator->() noexcept { return &value; }
template<typename T, bool F>
const T* Root<T, F>::operator->() const noexcept { return &value; }

template<typename T>
T& Root<Ptr<T>>::operator*() const { return *value; }
template<typename T>
T* Root<Ptr<T>>::operator->() const { return value.operator->(); }

template<typename T, typename... Args>
Root<Ptr<T>> Collector::alloc(Args&&... args) {
	static_assert(Traceable<T>::enabled,
			"Objects managed by the gc need to implement Traceable");
	if (allocations >= treshold) {
		collect();
		treshold = std::max(allocations * 2, size_t(128));
	}
	auto box = new detail::TBox<T>();
	box->valid = true;
	box->marked = false;
	box->ptrs = 0;
	box->next = box_head;
	new(box->value) T(std::forward<Args>(args)...);
	box_head = box;
	allocations += 1;
	return root(Ptr(box));
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
