#include "gc.h"

#include <vector>

Tracer::Tracer(std::function<void(detail::Box*)> callback)
	: callback(callback)
{}

Collector::Collector()
	: box_head(nullptr)
	, allocations(0)
	, treshold(128)
{}

Collector::~Collector() {
	collect();
}

Collector::Collector(Collector&& other)
	: Collector()
{
	swap(*this, other);
}

Collector& Collector::operator=(Collector other) {
	swap(*this, other);
	return *this;
}

void swap(Collector& a, Collector& b) {
	std::swap(a.box_head, b.box_head);
	std::swap(a.allocations, b.allocations);
	std::swap(a.treshold, b.treshold);
}

void Collector::collect() {
	// mark
	std::vector<detail::Box*> queue;
	auto tracer = Tracer([&](auto box) {
		if (!box->marked) {
			box->marked = true;
			queue.push_back(box);
		}
	});
	for (auto box = box_head; box != nullptr; box = box->next) {
		if (box->roots > 0) {
			box->marked = true;
			queue.push_back(box);
		}
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
