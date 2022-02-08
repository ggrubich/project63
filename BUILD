compiler_opts = ["-std=c++17", "-Wall", "-Wextra", "-pedantic-errors"]

cc_library(
    name = "lib",
    srcs = [
        "builtins.cpp",
        "compiler.cpp",
        "gc.cpp",
        "parser.cpp",
        "strings.cpp",
        "value.cpp",
        "vm.cpp"
    ],
    hdrs = [
        "builtins.h",
        "compiler.h",
        "gc.h",
        "parser.h",
        "strings.h",
        "value.h",
        "variant.h",
        "vm.h"
    ],
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
    srcs = [
        "compiler_test.cpp",
        "gc_test.cpp",
        "parser_test.cpp",
        "strings_test.cpp",
        "value_test.cpp",
        "vm_test.cpp"
    ],
    deps = [":lib", "@googletest//:gtest_main"],
    copts = compiler_opts,
)
