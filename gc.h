#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>

class Tracer;
template<typename T> class Ptr;
template<typename T> class Root;

namespace detail {

// Internal node used by the garbage collector.
template<typename T>
struct Box {
	bool marked;
	// Number of existing roots.
	uint64_t roots;
	// Number of existing weak pointers.
	uint64_t ptrs;
	void (*trace)(Box<void>*, Tracer&);
	void (*free)(Box<void>*);
	// Linked list of all boxes.
	Box<void>* next;
	// Data itself. Can be null if deallocated.
	T* value;
};

}  // namespace detail

// Visitor used for tracing objects.
class Tracer {
	friend class Collector;

private:
	std::function<void(detail::Box<void>*)> callback;

	explicit Tracer(std::function<void(detail::Box<void>*)>);

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
	friend class Collector;

private:
	detail::Box<T>* box;

	explicit Ptr(detail::Box<T>* box);

public:
	Ptr();
	~Ptr();
	Ptr(const Ptr<T>&);
	Ptr(Ptr<T>&&);
	Ptr& operator=(Ptr<T>);

	// Checks if the pointer is valid.
	bool valid() const;

	// Attempts to root the pointer. Rooting an invalid pointer throws an exception.
	Root<T> rooted();
};

// RAII guard representing a rooted pointer. Existence of a Root guarantees
// that the underlying pointer will not be invalidated for the duration of its
// lifetime.
// Contained data can be accessed by dereferencing the root.
template<typename T>
class Root {
	template<typename U> friend class Ptr;

private:
	detail::Box<T>* box;

	explicit Root(detail::Box<T>* box);

public:
	// Root is supposed to be used as a stack-only guard.
	// Copy and move constructors are deleted to make creation of root cycles
	// impossible, as those would lead to memory leaks.
	Root(const Root<T>&) = delete;
	Root(Root<T>&&) = delete;
	Root& operator=(Root<T>) = delete;

	~Root();

	T& operator*() const noexcept;
	T* operator->() const noexcept;

	Ptr<T> unrooted();
};

// The garbage collector object.
class Collector {
	friend class Tracer;
	template<typename T> friend class Ptr;
	template<typename T> friend class Root;

private:
	detail::Box<void>* box_head;
	size_t allocations;
	size_t treshold;

	template<typename T>
	static void do_trace(detail::Box<void>*, Tracer&);
	template<typename T>
	static void do_free(detail::Box<void>*);

public:
	Collector();
	~Collector();

	Collector(const Collector&) = delete;
	Collector(Collector&&);
	Collector& operator=(Collector);

	friend void swap(Collector&, Collector&);

	// Allocates a new gc managed pointer.
	template<typename T>
	Ptr<T> alloc(T value);

	// Triggers a gc cycle.
	void collect();
};

// Implementations

template<typename T>
void Tracer::visit(Ptr<T> ptr) {
	callback((detail::Box<void>*)ptr.box);
}

template<typename T>
Ptr<T>::Ptr(detail::Box<T>* box) : box(box) {
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
	return box != nullptr && box->value != nullptr;
}

template<typename T>
Root<T> Ptr<T>::rooted() {
	if (!valid()) {
		throw std::runtime_error("can't root invalid Ptr");
	}
	return Root(box);
}

template<typename T>
Root<T>::Root(detail::Box<T>* box) : box(box) {
	box->roots += 1;
}

template<typename T>
Root<T>::~Root() {
	box->roots -= 1;
}

template<typename T>
T& Root<T>::operator*() const noexcept {
	return *box->value;
}

template<typename T>
T* Root<T>::operator->() const noexcept {
	return box->value;
}

template<typename T>
Ptr<T> Root<T>::unrooted() {
	return Ptr(box);
}

template<typename T>
void Collector::do_trace(detail::Box<void>* untyped, Tracer& t) {
	static_assert(Traceable<T>::enabled);
	auto typed = (detail::Box<T>*)untyped;
	Traceable<T>::trace(*typed->value, t);
}

template<typename T>
void Collector::do_free(detail::Box<void>* untyped) {
	auto typed = (detail::Box<T>*)untyped;
	delete typed->value;
}

template<typename T>
Ptr<T> Collector::alloc(T value) {
	if (allocations >= treshold) {
		collect();
		treshold = std::max(allocations * 2, size_t(128));
	}
	auto box = new detail::Box<T>();
	box->marked = false;
	box->roots = 0;
	box->ptrs = 0;
	box->trace = do_trace<T>;
	box->free = do_free<T>;
	box->next = box_head;
	box->value = new T(std::move(value));
	box_head = (detail::Box<void>*)box;
	allocations += 1;
	return Ptr(box);
}
