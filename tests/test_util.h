/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef KSUID_TEST_UTIL_H
#define KSUID_TEST_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ksuid_test_failures_ = 0;

#define RUN_TEST(fn) do {                                                   \
    int before_ = ksuid_test_failures_;                                     \
    fprintf(stderr, "[RUN ] %s\n", #fn);                                    \
    fn();                                                                   \
    fprintf(stderr, "[%s] %s\n",                                            \
        (ksuid_test_failures_ == before_) ? " OK " : "FAIL", #fn);          \
} while (0)

#define FAIL_(msg) do {                                                     \
    fprintf(stderr, "  ASSERT %s:%d: %s\n", __FILE__, __LINE__, msg);       \
    ksuid_test_failures_++;                                                 \
} while (0)

#define ASSERT_TRUE(x) do {                                                 \
    if (!(x)) FAIL_(#x);                                                    \
} while (0)

#define ASSERT_FALSE(x) do {                                                \
    if (x) FAIL_("!(" #x ")");                                              \
} while (0)

#define ASSERT_EQ_INT(a, b) do {                                            \
    long long a_ = (long long)(a), b_ = (long long)(b);                     \
    if (a_ != b_) {                                                         \
        fprintf(stderr, "  %lld != %lld\n", a_, b_);                        \
        FAIL_(#a " == " #b);                                                \
    }                                                                       \
} while (0)

#define ASSERT_EQ_STR(a, b) do {                                            \
    if (strcmp((a), (b)) != 0) {                                            \
        fprintf(stderr, "  \"%s\" != \"%s\"\n", (a), (b));                  \
        FAIL_(#a " == " #b);                                                \
    }                                                                       \
} while (0)

#define ASSERT_EQ_STRN(a, b, n) do {                                        \
    if (memcmp((a), (b), (n)) != 0) {                                       \
        fprintf(stderr, "  \"%.*s\" != \"%.*s\"\n",                         \
                (int)(n), (a), (int)(n), (b));                              \
        FAIL_(#a " == " #b);                                                \
    }                                                                       \
} while (0)

#define ASSERT_EQ_BYTES(a, b, n) do {                                       \
    if (memcmp((a), (b), (n)) != 0) {                                       \
        FAIL_(#a " == " #b " (" #n " bytes)");                              \
        for (size_t i_ = 0; i_ < (size_t)(n); ++i_) {                       \
            fprintf(stderr, "  [%zu] 0x%02x vs 0x%02x\n",                   \
                    i_,                                                     \
                    ((const unsigned char *)(a))[i_],                       \
                    ((const unsigned char *)(b))[i_]);                      \
        }                                                                   \
    }                                                                       \
} while (0)

#define TEST_MAIN_END() return ksuid_test_failures_ ? EXIT_FAILURE : EXIT_SUCCESS

#endif /* KSUID_TEST_UTIL_H */
