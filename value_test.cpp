#include "value.h"

#include <gtest/gtest.h>

void define_string(Context& ctx, const Ptr<Klass>& cls,
		const std::string& name, const std::string& value)
{
	cls->define(ctx, name, *ctx.alloc<std::string>(value));
}

std::optional<std::string> lookup_string(const Ptr<Klass>& cls, const std::string& name) {
	auto found = cls->lookup(name);
	if (found) {
		return *found->get<Ptr<std::string>>();
	}
	else {
		return std::nullopt;
	}
}

TEST(ValueTest, MethodLookup) {
	Context ctx;

	auto base = ctx.alloc<Klass>(Ptr<Klass>(), std::nullopt);
	auto middle = ctx.alloc<Klass>(Ptr<Klass>(), *base);
	auto derived = ctx.alloc<Klass>(Ptr<Klass>(), *middle);

	EXPECT_EQ(lookup_string(*derived, "foo"), std::nullopt);
	// Insert at the base.
	define_string(ctx, *base, "foo", "base");
	EXPECT_EQ(lookup_string(*derived, "foo"), std::string("base"));
	// Insert in the middle. This sould invalidate the caches below.
	define_string(ctx, *middle, "foo", "middle");
	EXPECT_EQ(lookup_string(*derived, "foo"), std::string("middle"));
	// Change the value in middle.
	define_string(ctx, *middle, "foo", "middle2");
	EXPECT_EQ(lookup_string(*derived, "foo"), std::string("middle2"));
	// Remove the value from the middle. This should expose base again.
	(*middle)->remove("foo");
	EXPECT_EQ(lookup_string(*derived, "foo"), std::string("base"));
}
