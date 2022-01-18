#include "gc.h"

namespace detail {

BoxBase::BoxBase(uint8_t offset, BoxBase* next)
	: valid(true)
	, marked(false)
	, offset(offset)
	, ptrs(0)
	, next(next)
{}

RootBase::RootBase(RootBase** head) {
	attach(head);
}

RootBase::~RootBase() {
	detach();
}

RootBase::RootBase(const RootBase& other) noexcept : RootBase(other.head) {}

RootBase& RootBase::operator=(const RootBase& other) noexcept {
	// If both roots belong to the same linked list we don't need
	// to do anything as order of nodes doesn't matter.
	if (head != other.head) {
		detach();
		attach(other.head);
	}
	return *this;
}

void RootBase::attach(RootBase** head) {
	this->head = head;
	prev = nullptr;
	next = *head;
	if (next) {
		next->prev = this;
	}
	*head = this;
}

void RootBase::detach() {
	if (prev) {
		prev->next = next;
	}
	else {
		*head = next;
	}
	if (next) {
		next->prev = prev;
	}
}

}  // namespace detail

Collector::Collector()
	: box_head(nullptr)
	, root_head(nullptr)
	, allocations(0)
	, treshold(128)
{}

Collector::~Collector() {
	collect();
}

void Collector::collect() {
	// mark
	std::vector<detail::BoxBase*> queue;
	auto tracer = Tracer([&](const auto& ptr) {
		if (ptr.valid()) {
			auto box = ptr.box;
			if (!box->marked) {
				box->marked = true;
				queue.push_back(box);
			}
		}
	});
	for (auto root = root_head; root != nullptr; root = root->next) {
		root->trace(tracer);
	}
	while (!queue.empty()) {
		auto box = queue.back();
		queue.pop_back();
		box->trace(tracer);
	}
	// sweep
	auto box = box_head;
	box_head = nullptr;
	while (box != nullptr) {
		auto next = box->next;
		if (box->marked) {
			box->marked = false;
			box->next = box_head;
			box_head = box;
		}
		else {
			if (box->valid) {
				box->destroy();
				box->valid = false;
			}
			if (box->ptrs == 0) {
				delete box;
				allocations -= 1;
			}
			else {
				box->next = box_head;
				box_head = box;
			}
		}
		box = next;
	}
}
