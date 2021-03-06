#include "gc.h"

#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

namespace {

class Count {
private:
	std::shared_ptr<int64_t> value;

public:
	Count(int64_t initial);
	Count();
	~Count();
	Count(const Count&);
	Count& operator=(Count);

	int64_t get() const;
};

Count::Count(int64_t initial) : value(std::make_shared<int64_t>(initial)) {}

Count::Count() : Count(0) {}

Count::~Count() {
	*value -= 1;
}

Count::Count(const Count& other) : value(other.value) {
	*value += 1;
}

Count& Count::operator=(Count other) {
	std::swap(value, other.value);
	return *this;
}

int64_t Count::get() const {
	return *value;
}

struct Node {
	Count count;
	std::vector<Ptr<Node>> edges;

	Node(Count count);
	Node() = default;

	void add(Ptr<Node> x);

	Ptr<Node>& operator[](size_t idx);
	const Ptr<Node>& operator[](size_t idx) const;
};

Node::Node(Count count)
	: count(count)
	, edges()
{}

void Node::add(Ptr<Node> x) {
	edges.push_back(x);
}

Ptr<Node>& Node::operator[](size_t idx) {
	return edges[idx];
}

const Ptr<Node>& Node::operator[](size_t idx) const {
	return edges[idx];
}

}  // namespace anonymous

template<> struct Trace<Node> {
	void operator()(const Node& x, Tracer& t) {
		Trace<std::vector<Ptr<Node>>>()(x.edges, t);
	}
};

TEST(GcTest, LinkedList) {
	Count count;
	Collector gc;
	{
		auto head = gc.alloc<Node>(count);
		for (size_t i = 0; i < 5; ++i) {
			auto new_head = gc.alloc<Node>(count);
			(*new_head)->add(*head);
			head = new_head;
		}
		gc.collect();
		EXPECT_EQ(count.get(), 6) << "list should be alive";
	}
	gc.collect();
	EXPECT_EQ(count.get(), 0) << "list should be dead";
}

TEST(GcTest, Cycle) {
	Count count;
	Collector gc;
	{
		auto n1 = gc.alloc<Node>(count);
		{
			auto n2 = gc.alloc<Node>(count);
			auto n3 = gc.alloc<Node>(count);
			(*n1)->add(*n2);
			(*n2)->add(*n3);
			(*n3)->add(*n1);
			auto n4 = gc.alloc<Node>(count);
			(*n3)->add(*n4);
		}
		gc.collect();
		EXPECT_EQ(count.get(), 4) << "cycle should be alive";
	}
	gc.collect();
	EXPECT_EQ(count.get(), 0) << "cycle should be dead";
}

TEST(GcTest, Tree) {
	Count count;
	Collector gc;
	{
		auto root = gc.alloc<Node>(count);
		{
			auto n1 = root;
			auto n11 = gc.alloc<Node>(count);
			auto n12 = gc.alloc<Node>(count);
			(*n1)->add(*n11);
			(*n1)->add(*n12);
			auto n121 = gc.alloc<Node>(count);
			auto n122 = gc.alloc<Node>(count);
			auto n123 = gc.alloc<Node>(count);
			(*n12)->add(*n121);
			(*n12)->add(*n122);
			(*n12)->add(*n123);
		}
		gc.collect();  // root is n1
		EXPECT_EQ(count.get(), 6) << "entire tree should be alive";
		root = (**root)[1];
		gc.collect();  // root is n12
		EXPECT_EQ(count.get(), 4) << "part of the tree should be alive";
		root = (**root)[2];
		gc.collect();  // root is n123
		EXPECT_EQ(count.get(), 1) << "part of the tree should be alive";
	}
	gc.collect();
	EXPECT_EQ(count.get(), 0) << "tree should be dead";
}

TEST(GcTest, TraceInvalid) {
	Collector gc;
	Ptr<Node> p1;
	auto p2 = *gc.alloc<Node>();
	gc.collect();
	auto r1 = gc.root(p1);
	auto r2 = gc.root(p2);
	// p1 and p2 are now invalid and collector accessing them should hopefully
	// segfault the test.
	gc.collect();
}

TEST(GcTest, PtrValidity) {
	Collector gc;
	Ptr<Node> ptr;
	EXPECT_FALSE(ptr.valid()) << "empty ptr should be invaild";
	ptr = *gc.alloc<Node>();
	EXPECT_TRUE(ptr.valid()) << "freshly allocated ptr should be valid";
	{
		auto root = gc.root(ptr);
		gc.collect();
		EXPECT_TRUE(ptr.valid()) << "rooted ptr after gc should be valid";
	}
	gc.collect();
	EXPECT_FALSE(ptr.valid()) << "deallocated ptr should be invalid";
}

namespace {

struct Base {
	virtual ~Base() = default;
};

struct Left : Base {};

struct Right : Base {};

struct DerivedA : Left, Right {};

struct DerivedB : Base {};

}  // namespace anonymous

template<> struct Trace<DerivedA> {
	void operator()(const DerivedA&, Tracer&) {}
};

TEST(GcTest, PtrCasts) {
	Collector gc;
	auto derived = *gc.alloc<DerivedA>();
	Ptr<Base> base = Ptr<Left>(derived);  // correct upcast
	EXPECT_THROW({
		Ptr<Right> right = derived;
	}, std::bad_cast) << "upcasting to second base class should throw";
	EXPECT_TRUE(bool(base.dyncast<DerivedA>())) <<
		"downcasting to actual type should succeed";
	EXPECT_FALSE(bool(base.dyncast<DerivedB>())) <<
		"downcasting to other derived type should fail";
	EXPECT_FALSE(bool(base.dyncast<Right>())) <<
		"downcasting to second base should fail";
	Ptr<Right> unchecked = Ptr<void>(Ptr<void>(derived)).cast<Right>();  // unchecked cast
}
