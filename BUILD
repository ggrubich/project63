compiler_opts = ["-std=c++17", "-Wall", "-Wextra", "-pedantic-errors"]

cc_library(
    name = "lib",
    srcs = ["gc.cpp"],
    hdrs = ["gc.h"],
    copts = compiler_opts,
)

cc_binary(
    name = "main",
    srcs = ["main.cpp"],
    deps = [":lib"],
    copts = compiler_opts,
)

cc_test(
    name = "test",
    size = "small",
    srcs = ["gc_test.cpp"],
    deps = [":lib", "@googletest//:gtest_main"],
    copts = compiler_opts,
)
