/**
  ******************************************************************************
  * @file           : test_format.c
  * @brief          : Typed-argument formatter (logging_format.c): standard
  *                   conversions against glibc snprintf, ArduinoLog extensions,
  *                   argument-type handling and error markers
  * @note           : Arguments are passed as kind and value arrays
  ******************************************************************************
  */

#include "logging_format.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

// Some cases deliberately use flags that printf ignores ("%08.3d", "%-08d"),
// to check that the library ignores them the same way.
#pragma GCC diagnostic ignored "-Wformat"

static int g_failures = 0;
static int g_checks = 0;

typedef struct {
    unsigned char kind;
    log_value_t v;
} targ_t;

static targ_t I(long long v, int size) {
    targ_t a; memset(&a, 0, sizeof(a));
    if (size > 4) { a.kind = LOG_VAL_I64; a.v.i64 = v; } else { a.kind = LOG_VAL_I32; a.v.i32 = (int32_t)v; }
    return a;
}
static targ_t U(unsigned long long v, int size) {
    targ_t a; memset(&a, 0, sizeof(a));
    if (size > 4) { a.kind = LOG_VAL_U64; a.v.u64 = v; } else { a.kind = LOG_VAL_U32; a.v.u32 = (uint32_t)v; }
    return a;
}
static targ_t D(double v) { targ_t a; memset(&a, 0, sizeof(a)); a.kind = LOG_VAL_DOUBLE; a.v.d = v; return a; }
static targ_t S(const char *v) { targ_t a; memset(&a, 0, sizeof(a)); a.kind = LOG_VAL_STRING; a.v.s = v; return a; }
static targ_t B(int v) { targ_t a; memset(&a, 0, sizeof(a)); a.kind = LOG_VAL_BOOL; a.v.u32 = v ? 1u : 0u; return a; }
static targ_t P(const void *v) { targ_t a; memset(&a, 0, sizeof(a)); a.kind = LOG_VAL_POINTER; a.v.p = v; return a; }
static targ_t IP(unsigned a0, unsigned a1, unsigned a2, unsigned a3) {
    targ_t a; memset(&a, 0, sizeof(a)); a.kind = LOG_VAL_IPV4;
    a.v.u32 = a0 | (a1 << 8) | (a2 << 16) | ((uint32_t)a3 << 24); return a;
}

static size_t format_targs(char *buf, size_t size, const char *fmt, const targ_t *args, size_t n, unsigned opts) {
    unsigned char kinds[16];
    log_value_t values[16];
    for (size_t i = 0; i < n && i < 16; i++) {
        kinds[i] = args[i].kind;
        values[i] = args[i].v;
    }
    return log_format_values(buf, size, fmt, n ? kinds : NULL, n ? values : NULL, n, opts);
}

static void expect(const char *fmt, const targ_t *args, size_t n, unsigned opts, const char *want) {
    char got[256];
    format_targs(got, sizeof(got), fmt, args, n, opts);
    g_checks++;
    if (strcmp(got, want) != 0) {
        printf("FAIL format \"%s\" (opts %u): got \"%s\", want \"%s\"\n", fmt, opts, got, want);
        g_failures++;
    }
}

#define A(...) ((const targ_t[]){ __VA_ARGS__ })
#define N(...) (sizeof((const targ_t[]){ __VA_ARGS__ }) / sizeof(targ_t))
#define EXPECT(fmt, opts, want, ...) expect(fmt, A(__VA_ARGS__), N(__VA_ARGS__), opts, want)
#define EXPECT0(fmt, opts, want) expect(fmt, NULL, 0, opts, want)

// Compare against glibc snprintf with the same format and C arguments
#define SAME(fmt, cvalue, arg)                                 \
    do {                                                       \
        char want_[256];                                       \
        snprintf(want_, sizeof(want_), fmt, cvalue);           \
        EXPECT(fmt, 0, want_, arg);                            \
        EXPECT(fmt, LOG_FORMAT_ARDUINOLOG, want_, arg);        \
    } while (0)

int main(void) {
    int x = 0;

    // ---- Standard conversions against glibc (with and without ArduinoLog mode)
    SAME("%d", 42, I(42, 4));
    SAME("%d", -42, I(-42, 4));
    SAME("[%5d]", 42, I(42, 4));
    SAME("[%-5d]", 42, I(42, 4));
    SAME("[%05d]", -42, I(-42, 4));
    SAME("[%+d]", 42, I(42, 4));
    SAME("[% d]", 42, I(42, 4));
    SAME("[%.3d]", 7, I(7, 4));
    SAME("[%8.3d]", -7, I(-7, 4));
    SAME("%i", 123, I(123, 4));
    SAME("%u", 4000000000u, U(4000000000u, 4));
    SAME("%x", 0x1a2b, U(0x1a2b, 4));
    SAME("%04x", 26, I(26, 4));
    SAME("%08x", 0xdeadu, U(0xdeadu, 2));
    SAME("%#x", 255, I(255, 4));
    SAME("%#o", 8, I(8, 4));
    SAME("%o", 511, I(511, 4));
    SAME("%08lx", 0x12345678UL, U(0x12345678UL, sizeof(long)));
    SAME("%lld", -5000000000000000LL, I(-5000000000000000LL, 8));
    SAME("%llu", 18446744073709551615ULL, U(18446744073709551615ULL, 8));
    SAME("%hhx", 0x34, I(0x1234, 4));
    SAME("%hd", (short)70000, I(70000, 4));
    SAME("%c", 'A', I('A', 1));
    SAME("[%3c]", 'z', I('z', 1));
    SAME("[%-3c]", 'z', I('z', 1));
    SAME("%s", "abc", S("abc"));
    SAME("[%-6s]", "abc", S("abc"));
    SAME("[%6s]", "abc", S("abc"));
    SAME("[%.2s]", "abcdef", S("abcdef"));
    SAME("[%8.3s]", "abcdef", S("abcdef"));
    SAME("%f", 3.14159, D(3.14159));
    SAME("%.2f", 3.14159, D(3.14159));
    SAME("[%10.3f]", -3.14159, D(-3.14159));
    SAME("[%-10.1f]", 2.5, D(2.5));
    SAME("[%+.1f]", 2.25, D(2.25));
    SAME("%e", 12345.678, D(12345.678));
    SAME("%E", 0.000123, D(0.000123));
    SAME("%g", 0.0001, D(0.0001));
    SAME("%G", 1e20, D(1e20));
    SAME("%a", 1.5, D(1.5));
    SAME("%p", (void *)&x, P(&x));

    // ---- Integer flags, formatted by the library, against glibc
    SAME("[%+5d]", 42, I(42, 4));
    SAME("[%-+5d]", 42, I(42, 4));
    SAME("[% 05d]", 42, I(42, 4));
    SAME("[%+.3d]", 7, I(7, 4));
    SAME("[%08.3d]", 5, I(5, 4));
    SAME("[%-08d]", 5, I(5, 4));
    SAME("[%.0d]", 0, I(0, 4));
    SAME("[%5.0d]", 0, I(0, 4));
    SAME("[%d]", 0, I(0, 4));
    SAME("[%u]", 0u, U(0, 4));
    SAME("[%x]", 0u, U(0, 4));
    SAME("[%#x]", 0u, U(0, 4));
    SAME("[%#o]", 0u, U(0, 4));
    SAME("[%#.0o]", 0u, U(0, 4));
    SAME("[%#o]", 8u, U(8, 4));
    SAME("[%#08x]", 255u, U(255, 4));
    SAME("[%-#8x]", 255u, U(255, 4));
    SAME("[%#X]", 255u, U(255, 4));
    SAME("[%12.8x]", 0xabcu, U(0xabc, 4));
    SAME("%lld", (long long)(-9223372036854775807LL - 1), I(-9223372036854775807LL - 1, 8));
    SAME("%llo", 18446744073709551615ULL, U(18446744073709551615ULL, 8));
    SAME("%llX", 18446744073709551615ULL, U(18446744073709551615ULL, 8));
    SAME("[%-6c]", 'q', I('q', 1));
    SAME("[%.0s]", "abc", S("abc"));
    SAME("[%-10p]", (void *)&x, P(&x));

    // ---- Width and precision from arguments
    EXPECT("[%*d]", 0, "[   42]", I(5, 4), I(42, 4));
    EXPECT("[%*d]", 0, "[42   ]", I(-5, 4), I(42, 4));
    EXPECT("[%.*f]", 0, "[3.1]", I(1, 4), D(3.14159));
    EXPECT("[%*.*s]", 0, "[   ab]", I(5, 4), I(2, 4), S("abcdef"));
    EXPECT("[%.*d]", 0, "[42]", I(-1, 4), I(42, 4));

    // ---- Values taken from the real argument type
    EXPECT("%d", 0, "5000000000", I(5000000000LL, 8));            // no truncation to int
    EXPECT("%x", 0, "ffffffff", I(-1, 1));                         // int8 -1 promoted as in C
    EXPECT("%hhx", 0, "ff", I(-1, 1));
    EXPECT("%d", 0, "-1", U(0xFFFFFFFFu, 4));                       // printf reinterpretation
    EXPECT("%u", 0, "4294967295", I(-1, 4));
    EXPECT("%ld", 0, "-3", I(-3, 1));
    EXPECT("%d", 0, "1", B(1));
    EXPECT("%f", 0, "7.000000", I(7, 4));                           // integer to double
    EXPECT("%s", 0, "192.168.1.10", IP(192, 168, 1, 10));
    EXPECT("%s", 0, "(null)", S(NULL));

    // ---- Error markers and edge cases
    EXPECT("%d %d", 0, "1 <?>", I(1, 4));
    EXPECT("%d", 0, "<!>", S("text"));
    EXPECT("%s", 0, "<!>", I(5, 4));
    EXPECT("%c", 0, "<!>", S("x"));
    EXPECT("%d", 0, "<!>", D(1.5));
    EXPECT("%n", 0, "<!>", P(&x));
    EXPECT("a %d b", 0, "a 1 b", I(1, 4), I(2, 4));                  // surplus ignored
    EXPECT("%q %d", 0, "%q 5", I(5, 4));                             // unknown printed literally
    EXPECT0("100%", 0, "100%");
    EXPECT0("x%-", 0, "x%-");
    EXPECT0("50%% off", 0, "50% off");
    expect(NULL, NULL, 0, 0, "(null format)");

    // ---- ArduinoLog extensions (always available)
    EXPECT("%t %t", 0, "T F", B(1), B(0));
    EXPECT("%T %T", 0, "true false", I(2, 4), I(0, 4));
    EXPECT("[%-5t]", 0, "[T    ]", B(1));
    EXPECT("%b", 0, "101", I(5, 4));
    EXPECT("%B", 0, "0b101", I(5, 4));
    EXPECT("%08b", 0, "00000101", U(5, 1));
    EXPECT("%#010b", 0, "0b00000101", U(5, 1));
    EXPECT("[%-6b]", 0, "[101   ]", I(5, 4));
    EXPECT("[%6b]", 0, "[   101]", I(5, 4));
    EXPECT("%.4b", 0, "0101", I(5, 4));
    EXPECT("%b", 0, "0", I(0, 4));
    EXPECT("%S", 0, "text", S("text"));
    EXPECT("[%-8S]", 0, "[ab      ]", S("ab"));
    EXPECT("%P", 0, "flash", S("flash"));
    EXPECT("%I", 0, "10.0.0.1", IP(10, 0, 0, 1));
    EXPECT("[%16I]", 0, "[        10.0.0.1]", IP(10, 0, 0, 1));
    EXPECT("%.3D", 0, "3.142", D(3.14159));
    EXPECT("%td", 0, "-9", I(-9, sizeof(ptrdiff_t)));               // printf length modifier still works
    EXPECT("%zu", 0, "17", U(17, sizeof(size_t)));
    EXPECT("%h", 0, "%h", I(1, 4));                                   // lone length letter: unknown

    // ---- Bare conversions: ArduinoLog meaning only with LOG_FORMAT_ARDUINOLOG
    EXPECT("%X", LOG_FORMAT_ARDUINOLOG, "0x1A", I(26, 4));
    EXPECT("%X", 0, "1A", I(26, 4));
    EXPECT("%04X", LOG_FORMAT_ARDUINOLOG, "001A", I(26, 4));
    EXPECT("%x", LOG_FORMAT_ARDUINOLOG, "1a", I(26, 4));
    EXPECT("0x%x", LOG_FORMAT_ARDUINOLOG, "0x1a", I(26, 4));
    EXPECT("%F", LOG_FORMAT_ARDUINOLOG, "3.14", D(3.14159));
    EXPECT("%F", 0, "3.141590", D(3.14159));
    EXPECT("%.1F", LOG_FORMAT_ARDUINOLOG, "3.1", D(3.14159));
    EXPECT("%D", LOG_FORMAT_ARDUINOLOG, "3.14", D(3.14159));
    EXPECT("%F", LOG_FORMAT_ARDUINOLOG, "2.50", D(2.5f));
    // Bare %F follows Arduino Print::print(double, 2), as ArduinoLog did
    EXPECT("%F", LOG_FORMAT_ARDUINOLOG, "-1.50", D(-1.5));
    EXPECT("%F", LOG_FORMAT_ARDUINOLOG, "0.00", D(0.0));
    EXPECT("%F", LOG_FORMAT_ARDUINOLOG, "1.00", D(0.999));
    EXPECT("%F", LOG_FORMAT_ARDUINOLOG, "123.46", D(123.456));
    EXPECT("%F", LOG_FORMAT_ARDUINOLOG, "nan", D(0.0 / 0.0));
    EXPECT("%F", LOG_FORMAT_ARDUINOLOG, "inf", D(1.0 / 0.0));
    EXPECT("%F", LOG_FORMAT_ARDUINOLOG, "ovf", D(5e9));
    EXPECT("%p", 0, "0x0", P(NULL));
    EXPECT("heap = %l\n", LOG_FORMAT_ARDUINOLOG, "heap = 123456\n", I(123456, 4));
    EXPECT("=%l", LOG_FORMAT_ARDUINOLOG, "=123", I(123, 4));
    EXPECT("t=%lu ms", LOG_FORMAT_ARDUINOLOG, "t=5000 ms", U(5000, 4));
    EXPECT("%lx", LOG_FORMAT_ARDUINOLOG, "ff", U(255, 4));
    EXPECT("%lf", LOG_FORMAT_ARDUINOLOG, "1.500000", D(1.5));
    EXPECT("v=%04x", LOG_FORMAT_ARDUINOLOG, "v=001a", I(26, 4));
    EXPECT("k=%-8S|", LOG_FORMAT_ARDUINOLOG, "k=ab      |", S("ab"));
    EXPECT("p=%.1F", LOG_FORMAT_ARDUINOLOG, "p=3.1", D(3.14159));
    EXPECT("ok=%t", LOG_FORMAT_ARDUINOLOG, "ok=T", B(1));

    // ---- Truncation keeps a terminated, clamped result
    {
        char small[8];
        targ_t a = S("abcdefghijkl");
        size_t n = format_targs(small, sizeof(small), "[%s]", &a, 1, 0);
        g_checks++;
        if (n != 7 || strcmp(small, "[abcdef") != 0) {
            printf("FAIL truncation: got \"%s\" (%zu)\n", small, n);
            g_failures++;
        }
    }

    printf("%s %d format checks, %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
