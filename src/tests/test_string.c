#include "tests/test_runner.h"

#include <stddef.h>
#include <stdint.h>

#include "lib/string.h"

void tests_string(void) {
    char buf[64];
    char argv_buf[64];
    char *argv[8];
    int argc;

    /* str_len */
    test_assert(str_len("") == 0,       "str_len empty");
    test_assert(str_len("hello") == 5,  "str_len basic");
    test_assert(str_len("abc") == 3,    "str_len short");

    /* str_cmp */
    test_assert(str_cmp("abc", "abc") == 0, "str_cmp equal");
    test_assert(str_cmp("a", "b") < 0,      "str_cmp less");
    test_assert(str_cmp("b", "a") > 0,      "str_cmp greater");

    /* str_ncmp */
    test_assert(str_ncmp("abcX", "abcY", 3) == 0,  "str_ncmp prefix equal");
    test_assert(str_ncmp("abc", "abc", 3) == 0,     "str_ncmp full equal");
    test_assert(str_ncmp("a", "b", 1) < 0,          "str_ncmp less");
    test_assert(str_ncmp("b", "a", 1) > 0,          "str_ncmp greater");

    /* str_eq */
    test_assert(str_eq("hello", "hello") == 1,  "str_eq match");
    test_assert(str_eq("hello", "world") == 0,  "str_eq no match");
    test_assert(str_eq("", "") == 1,             "str_eq empty");

    /* str_copy */
    buf[0] = '\0';
    str_copy(buf, "hello", sizeof(buf));
    test_assert(str_eq(buf, "hello"), "str_copy basic");

    buf[0] = '\0';
    str_copy(buf, "toolong", 4);  /* dst_size=4: copies 3 chars + NUL */
    test_assert(str_eq(buf, "too"), "str_copy truncate");

    /* str_cat */
    buf[0] = '\0';
    str_copy(buf, "foo", sizeof(buf));
    str_cat(buf, "bar", sizeof(buf));
    test_assert(str_eq(buf, "foobar"), "str_cat basic");

    buf[0] = '\0';
    str_copy(buf, "ab", 4);
    str_cat(buf, "cde", 4);  /* only room for 1 more char + NUL */
    test_assert(str_eq(buf, "abc"), "str_cat truncate");

    /* mem_set */
    mem_set(buf, 0xFF, 4);
    test_assert((unsigned char)buf[0] == 0xFF, "mem_set value");
    test_assert((unsigned char)buf[3] == 0xFF, "mem_set last byte");

    mem_set(buf, 0, sizeof(buf));
    test_assert(buf[0] == 0, "mem_set zero");

    /* mem_copy */
    const char src[] = "testdata";
    mem_copy(buf, src, str_len(src) + 1);
    test_assert(str_eq(buf, "testdata"), "mem_copy basic");

    /* u32_to_dec */
    u32_to_dec(0, buf, sizeof(buf));
    test_assert(str_eq(buf, "0"), "u32_to_dec zero");

    u32_to_dec(1, buf, sizeof(buf));
    test_assert(str_eq(buf, "1"), "u32_to_dec one");

    u32_to_dec(12345, buf, sizeof(buf));
    test_assert(str_eq(buf, "12345"), "u32_to_dec multi-digit");

    u32_to_dec(4294967295u, buf, sizeof(buf));
    test_assert(str_eq(buf, "4294967295"), "u32_to_dec max");

    /* split_args */
    str_copy(argv_buf, "echo hello world", sizeof(argv_buf));
    argc = split_args(argv_buf, argv, 8);
    test_assert(argc == 3,                  "split_args count");
    test_assert(str_eq(argv[0], "echo"),    "split_args arg0");
    test_assert(str_eq(argv[1], "hello"),   "split_args arg1");
    test_assert(str_eq(argv[2], "world"),   "split_args arg2");

    str_copy(argv_buf, "  spaces  ", sizeof(argv_buf));
    argc = split_args(argv_buf, argv, 8);
    test_assert(argc == 1, "split_args leading/trailing spaces");
}
