#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>

class Tracer;
template<typename T> class Ptr;
template<typename T> class Root;

namespace detail {

// Base class for internal nodes used by the garbage collector.
struct Box {
	// True if contanied value was not destroyed yet.
	bool valid;
	// Switches to true after being visited in the mark phase.
	bool marked;
	// Number of existing roots.
	uint64_t roots;
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
	template<typename U> friend class Root;

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

	// Attempts to root the pointer. Rooting an invalid pointer throws an exception.
	Root<T> rooted() const;
	operator Root<T>() const;
};

// RAII guard representing a rooted pointer. Existence of a root guarantees
// that the underlying pointer and pointers reachable from it via tracing
// will not be invalidated for the duration of root's lifetime.
// Contained data can be accessed by dereferencing the root.
//
// Generally, roots should be used on the stack - in local or global variables.
// An object managed by gc should not contain references to roots - use Ptr for
// those instead. In particular, one should avoid creating root cycles, as those
// can cause memory leaks.
template<typename T>
class Root {
	template<typename U> friend class Ptr;
	friend class Collector;

private:
	detail::TBox<T>* box;

	explicit Root(detail::TBox<T>* box);

public:
	~Root();
	Root(const Root<T>&);
	Root& operator=(Root<T>);

	T& operator*() const noexcept;
	T* operator->() const noexcept;

	// Returns un unrooted version of the pointer.
	Ptr<T> unrooted() const;
	operator Ptr<T>() const;
};

// The garbage collector object.
class Collector {
private:
	detail::Box* box_head;
	size_t allocations;
	size_t treshold;

public:
	Collector();
	~Collector();

	Collector(const Collector&) = delete;
	Collector(Collector&&);
	Collector& operator=(Collector);

	friend void swap(Collector&, Collector&);

	// Allocates a new gc managed pointer.
	template<typename T, typename... Args>
	Root<T> alloc(Args&&... args);

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
Root<T> Ptr<T>::rooted() const {
	if (!valid()) {
		throw std::runtime_error("can't root invalid Ptr");
	}
	return Root(box);
}

template<typename T>
Ptr<T>::operator Root<T>() const {
	return rooted();
}

template<typename T>
Root<T>::Root(detail::TBox<T>* box) : box(box) {
	box->roots += 1;
}

template<typename T>
Root<T>::~Root() {
	box->roots -= 1;
}

template<typename T>
Root<T>::Root(const Root<T>& root) : Root(root.box) {}

template<typename T>
Root<T>& Root<T>::operator=(Root<T> root) {
	std::swap(box, root.box);
	return *this;
}

template<typename T>
T& Root<T>::operator*() const noexcept {
	return *reinterpret_cast<T*>(box->value);
}

template<typename T>
T* Root<T>::operator->() const noexcept {
	return reinterpret_cast<T*>(box->value);
}

template<typename T>
Ptr<T> Root<T>::unrooted() const {
	return Ptr(box);
}

template<typename T>
Root<T>::operator Ptr<T>() const {
	return unrooted();
}

template<typename T, typename... Args>
Root<T> Collector::alloc(Args&&... args) {
	static_assert(Traceable<T>::enabled,
			"Objects managed by the gc need to implement Traceable");
	if (allocations >= treshold) {
		collect();
		treshold = std::max(allocations * 2, size_t(128));
	}
	auto box = new detail::TBox<T>();
	box->valid = true;
	box->marked = false;
	box->roots = 0;
	box->ptrs = 0;
	box->next = box_head;
	new(box->value) T(std::forward<Args>(args)...);
	box_head = box;
	allocations += 1;
	return Root(box);
}
