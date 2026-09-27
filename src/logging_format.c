/**
  ******************************************************************************
  * @file           : logging_format.c
  * @brief          : Typed-argument printf formatter for logging framework
  * @note           : Integers, characters, strings, booleans, binary, IPv4 and
  *                   pointers are formatted by the library, without printf,
  *                   whose stack frame is large on small targets. Only float
  *                   conversions in printf syntax use snprintf().
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#include "logging_format.h"
#include "logging_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>

// *************************************************************************
//  Internal Argument Form
// *************************************************************************

typedef enum {
    LOG_ARG_INVALID = 0,
    LOG_ARG_INT,        // v.i, size in bytes
    LOG_ARG_UINT,       // v.u, size in bytes
    LOG_ARG_BOOL,       // v.u
    LOG_ARG_DOUBLE,     // v.d
    LOG_ARG_STRING,     // v.s
    LOG_ARG_POINTER,    // v.p
    LOG_ARG_IPV4        // v.u
} log_arg_type_t;

typedef struct {
    unsigned char type;
    unsigned char size;
    union {
        long long i;
        unsigned long long u;
        double d;
        const char *s;
        const void *p;
    } v;
} log_arg_t;

// *************************************************************************
//  Output Buffer
// *************************************************************************

typedef struct {
    char *buf;
    size_t size;
    size_t len;
} log_out_t;

static bool out_full(const log_out_t *o) {
    return o->len + 1 >= o->size;
}

static void out_str(log_out_t *o, const char *s, size_t n) {
    if (out_full(o)) {
        return;
    }
    size_t room = o->size - 1 - o->len;
    if (n > room) {
        n = room;
    }
    memcpy(o->buf + o->len, s, n);
    o->len += n;
    o->buf[o->len] = '\0';
}

static void out_cstr(log_out_t *o, const char *s) {
    out_str(o, s, strlen(s));
}

static void out_repeat(log_out_t *o, char c, int count) {
    for (int i = 0; i < count && !out_full(o); i++) {
        out_str(o, &c, 1);
    }
}

// Account for text that snprintf() wrote at the current position
static void out_advance(log_out_t *o, int n) {
    if (n <= 0) {
        o->buf[o->len] = '\0';
        return;
    }
    size_t room = o->size - 1 - o->len;
    o->len += ((size_t)n < room) ? (size_t)n : room;
}

// *************************************************************************
//  Conversion Specification
// *************************************************************************

typedef struct {
    char flags[6];      // subset of "-+ #0", terminated
    bool has_width;
    int width;
    bool has_prec;
    int prec;
    char length[3];     // "", "hh", "h", "l", "ll", "j", "z", "t", "L"
    char conv;
    bool bare;          // no flags, width, precision or length
} log_spec_t;

typedef struct {
    const unsigned char *kinds;
    const log_value_t *values;
    size_t count;
    size_t next;
    log_arg_t current;
} log_arg_iter_t;

/**
 * @brief Next argument in internal form (valid until the following call), NULL when missing
 */
static const log_arg_t *next_arg(log_arg_iter_t *it) {
    if (it->next >= it->count) {
        return NULL;
    }
    const log_value_t *v = &it->values[it->next];
    log_arg_t *a = &it->current;
    memset(a, 0, sizeof(*a));
    switch (it->kinds[it->next]) {
        case LOG_VAL_I32:     a->type = LOG_ARG_INT;     a->size = 4; a->v.i = v->i32; break;
        case LOG_VAL_U32:     a->type = LOG_ARG_UINT;    a->size = 4; a->v.u = v->u32; break;
        case LOG_VAL_I64:     a->type = LOG_ARG_INT;     a->size = 8; a->v.i = v->i64; break;
        case LOG_VAL_U64:     a->type = LOG_ARG_UINT;    a->size = 8; a->v.u = v->u64; break;
        case LOG_VAL_BOOL:    a->type = LOG_ARG_BOOL;    a->v.u = (v->u32 != 0u); break;
        case LOG_VAL_DOUBLE:  a->type = LOG_ARG_DOUBLE;  a->v.d = v->d; break;
        case LOG_VAL_STRING:  a->type = LOG_ARG_STRING;  a->v.s = v->s; break;
        case LOG_VAL_POINTER: a->type = LOG_ARG_POINTER; a->v.p = v->p; break;
        case LOG_VAL_IPV4:    a->type = LOG_ARG_IPV4;    a->v.u = v->u32; break;
        default:              a->type = LOG_ARG_INVALID; break;
    }
    it->next++;
    return a;
}

static bool has_flag(const log_spec_t *s, char flag) {
    return strchr(s->flags, flag) != NULL;
}

static void add_flag(log_spec_t *s, char flag) {
    size_t n = strlen(s->flags);
    if (!has_flag(s, flag) && n + 1 < sizeof(s->flags)) {
        s->flags[n] = flag;
        s->flags[n + 1] = '\0';
    }
}

/**
 * @brief Build a printf specification using '*' for width and precision
 *
 * @param keep_flags Flags to keep (NULL keeps all)
 */
static void build_spec(char *out, const log_spec_t *s, const char *keep_flags,
                       const char *length, char conv) {
    char *p = out;
    *p++ = '%';
    for (const char *f = s->flags; *f; f++) {
        if (!keep_flags || strchr(keep_flags, *f)) {
            *p++ = *f;
        }
    }
    if (s->has_width) {
        *p++ = '*';
    }
    if (s->has_prec) {
        *p++ = '.';
        *p++ = '*';
    }
    for (const char *l = length; *l; l++) {
        *p++ = *l;
    }
    *p++ = conv;
    *p = '\0';
}

// snprintf() one value at the current position, passing width and precision
#define LOG_PRINT_SPEC(o, spec, s, value)                                              \
    do {                                                                               \
        if (!out_full(o)) {                                                            \
            char *dst_ = (o)->buf + (o)->len;                                          \
            size_t room_ = (o)->size - (o)->len;                                       \
            int n_;                                                                    \
            if ((s)->has_width && (s)->has_prec) {                                     \
                n_ = snprintf(dst_, room_, spec, (s)->width, (s)->prec, value);        \
            } else if ((s)->has_width) {                                               \
                n_ = snprintf(dst_, room_, spec, (s)->width, value);                   \
            } else if ((s)->has_prec) {                                                \
                n_ = snprintf(dst_, room_, spec, (s)->prec, value);                    \
            } else {                                                                   \
                n_ = snprintf(dst_, room_, spec, value);                               \
            }                                                                          \
            out_advance(o, n_);                                                        \
        }                                                                              \
    } while (0)

// *************************************************************************
//  Integer Helpers
// *************************************************************************

static bool arg_is_integer(const log_arg_t *a) {
    return a->type == LOG_ARG_INT || a->type == LOG_ARG_UINT || a->type == LOG_ARG_BOOL;
}

static unsigned arg_bits(const log_arg_t *a) {
    if (a->type == LOG_ARG_BOOL || a->size == 0) {
        return (unsigned)(sizeof(int) * 8);
    }
    return (unsigned)a->size * 8u;
}

static unsigned long long arg_raw(const log_arg_t *a) {
    return (a->type == LOG_ARG_INT) ? (unsigned long long)a->v.i : a->v.u;
}

static unsigned spec_bits(const char *length) {
    if (strcmp(length, "hh") == 0) return 8u;
    if (strcmp(length, "h") == 0) return (unsigned)(sizeof(short) * 8);
    if (strcmp(length, "l") == 0) return (unsigned)(sizeof(long) * 8);
    if (strcmp(length, "ll") == 0 || strcmp(length, "j") == 0 || strcmp(length, "L") == 0) return 64u;
    if (strcmp(length, "z") == 0) return (unsigned)(sizeof(size_t) * 8);
    if (strcmp(length, "t") == 0) return (unsigned)(sizeof(ptrdiff_t) * 8);
    return (unsigned)(sizeof(int) * 8);
}

/**
 * @brief Width of the value: explicit hh/h truncates, otherwise never narrower than the argument
 */
static unsigned effective_bits(const log_spec_t *s, const log_arg_t *a) {
    unsigned sb = spec_bits(s->length);
    if (s->length[0] == 'h') {
        return sb;
    }
    unsigned ab = arg_bits(a);
    return (ab > sb) ? ab : sb;
}

static unsigned long long mask_bits(unsigned long long v, unsigned bits) {
    return (bits >= 64u) ? v : (v & ((1ULL << bits) - 1ULL));
}

static long long sign_extend(unsigned long long v, unsigned bits) {
    if (bits >= 64u) {
        return (long long)v;
    }
    v = mask_bits(v, bits);
    if ((v >> (bits - 1u)) & 1ULL) {
        v |= ~((1ULL << bits) - 1ULL);
    }
    return (long long)v;
}

// *************************************************************************
//  Conversions (the library formats everything except printf-style floats)
// *************************************************************************

/**
 * @brief Emit n characters padded to the width ('-' pads on the right)
 */
static void emit_padded(log_out_t *o, const log_spec_t *s, const char *str, size_t n) {
    int pad = (s->has_width && s->width > (int)n) ? s->width - (int)n : 0;
    bool left = has_flag(s, '-');
    if (!left) {
        out_repeat(o, ' ', pad);
    }
    out_str(o, str, n);
    if (left) {
        out_repeat(o, ' ', pad);
    }
}

/**
 * @brief Emit a string with printf %s rules: the precision limits its length
 */
static LOG_NOINLINE void emit_string(log_out_t *o, const log_spec_t *s, const char *str) {
    size_t n = 0;
    size_t max = s->has_prec ? (size_t)s->prec : (size_t)-1;
    while (n < max && str[n] != '\0') {
        n++;
    }
    emit_padded(o, s, str, n);
}

/**
 * @brief Digits of v in the given base, least significant first; 0 digits for 0
 */
static int to_digits(unsigned long long v, unsigned base, bool upper, char *digits) {
    // digits must hold 64 characters for base 2, 22 for base 8, 20 for base 10
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    while (v != 0u) {
        digits[n++] = set[v % base];
        v /= base;
    }
    return n;
}

/**
 * @brief Emit an integer with printf rules: [spaces] prefix [zeros] digits [spaces]
 *
 * @param prefix Sign or radix prefix ("-", "+", " ", "0x", "0X", "0b" or "")
 * @param digits Digits, least significant first
 * @param ndigits Number of digits (0 for a zero value)
 * @param alt_octal '#' with %o: the first digit printed must be 0
 */
static void emit_integer(log_out_t *o, const log_spec_t *s, const char *prefix,
                         const char *digits, int ndigits, bool alt_octal) {
    int min_digits = s->has_prec ? s->prec : 1;
    int zeros = (min_digits > ndigits) ? (min_digits - ndigits) : 0;
    if (alt_octal && zeros == 0) {
        zeros = 1;  // a non-zero octal value never starts with 0 by itself
    }
    int total = (int)strlen(prefix) + zeros + ndigits;
    int pad = (s->has_width && s->width > total) ? (s->width - total) : 0;

    bool left = has_flag(s, '-');
    bool zero_pad = has_flag(s, '0') && !left && !s->has_prec;
    if (pad > 0 && !left && !zero_pad) {
        out_repeat(o, ' ', pad);
    }
    out_cstr(o, prefix);
    if (zero_pad) {
        zeros += pad;  // zero padding goes after the sign or prefix
    }
    out_repeat(o, '0', zeros);
    for (int i = ndigits - 1; i >= 0; i--) {
        out_str(o, &digits[i], 1);
    }
    if (pad > 0 && left) {
        out_repeat(o, ' ', pad);
    }
}

static LOG_NOINLINE void emit_signed(log_out_t *o, const log_spec_t *s, const log_arg_t *a) {
    unsigned bits = effective_bits(s, a);
    long long v = sign_extend(arg_raw(a), bits);
    unsigned long long magnitude = (v < 0) ? (unsigned long long)(-(v + 1)) + 1u
                                           : (unsigned long long)v;
    const char *prefix = (v < 0) ? "-" : has_flag(s, '+') ? "+" : has_flag(s, ' ') ? " " : "";
    char digits[22];
    int n = to_digits(magnitude, 10u, false, digits);
    emit_integer(o, s, prefix, digits, n, false);
}

static LOG_NOINLINE void emit_unsigned(log_out_t *o, const log_spec_t *s, const log_arg_t *a,
                          unsigned int options) {
    unsigned bits = effective_bits(s, a);
    unsigned long long v = mask_bits(arg_raw(a), bits);
    unsigned base = (s->conv == 'o') ? 8u : (s->conv == 'u') ? 10u : 16u;

    const char *prefix = "";
    if ((options & LOG_FORMAT_ARDUINOLOG) && s->bare && s->conv == 'X') {
        prefix = "0x";  // ArduinoLog: bare %X prints "0x" + uppercase hex
    } else if (has_flag(s, '#') && v != 0u) {
        prefix = (s->conv == 'x') ? "0x" : (s->conv == 'X') ? "0X" : "";
    }

    char digits[22];
    int n = to_digits(v, base, s->conv == 'X', digits);
    emit_integer(o, s, prefix, digits, n, s->conv == 'o' && has_flag(s, '#'));
}

static LOG_NOINLINE void emit_binary(log_out_t *o, const log_spec_t *s, const log_arg_t *a) {
    unsigned bits = effective_bits(s, a);
    unsigned long long v = mask_bits(arg_raw(a), bits);
    const char *prefix = (s->conv == 'B' || has_flag(s, '#')) ? "0b" : "";
    char digits[64];
    int n = to_digits(v, 2u, false, digits);
    emit_integer(o, s, prefix, digits, n, false);
}

static LOG_NOINLINE void emit_pointer(log_out_t *o, const log_spec_t *s, const void *p) {
    char text[2 + 2 * sizeof(uintptr_t)];
    char digits[2 * sizeof(uintptr_t)];
    int n = to_digits((unsigned long long)(uintptr_t)p, 16u, false, digits);
    size_t len = 0;
    text[len++] = '0';
    text[len++] = 'x';
    if (n == 0) {
        text[len++] = '0';
    }
    while (n > 0) {
        text[len++] = digits[--n];
    }
    emit_padded(o, s, text, len);
}

static LOG_NOINLINE void format_ipv4(char *out, unsigned long long v) {
    size_t len = 0;
    for (int i = 0; i < 4; i++) {
        char digits[3];
        int n = to_digits((v >> (8 * i)) & 0xFFu, 10u, false, digits);
        if (n == 0) {
            out[len++] = '0';
        }
        while (n > 0) {
            out[len++] = digits[--n];
        }
        if (i < 3) {
            out[len++] = '.';
        }
    }
    out[len] = '\0';
}

/**
 * @brief ArduinoLog float output: the algorithm of Arduino Print::print(double, digits)
 */
static LOG_NOINLINE void emit_arduino_float(log_out_t *o, double number, int decimals) {
    if (isnan(number)) {
        out_cstr(o, "nan");
        return;
    }
    if (isinf(number)) {
        out_cstr(o, "inf");
        return;
    }
    if (number > 4294967040.0 || number < -4294967040.0) {
        out_cstr(o, "ovf");
        return;
    }
    if (number < 0.0) {
        out_cstr(o, "-");
        number = -number;
    }

    double rounding = 0.5;
    for (int i = 0; i < decimals; i++) {
        rounding /= 10.0;
    }
    number += rounding;

    unsigned long int_part = (unsigned long)number;
    double remainder = number - (double)int_part;
    char digits[20];
    int n = to_digits(int_part, 10u, false, digits);
    if (n == 0) {
        out_cstr(o, "0");
    }
    while (n > 0) {
        out_str(o, &digits[--n], 1);
    }
    if (decimals > 0) {
        out_cstr(o, ".");
    }
    while (decimals-- > 0) {
        remainder *= 10.0;
        unsigned int digit = (unsigned int)remainder;
        char c = (char)('0' + (int)(digit % 10u));
        out_str(o, &c, 1);
        remainder -= (double)digit;
    }
}

static LOG_NOINLINE void emit_double(log_out_t *o, const log_spec_t *s, double d, unsigned int options) {
    if ((options & LOG_FORMAT_ARDUINOLOG) && s->bare && (s->conv == 'F' || s->conv == 'D')) {
        emit_arduino_float(o, d, 2);  // ArduinoLog: bare %F and %D print 2 decimals
        return;
    }
    char spec[16];
    build_spec(spec, s, NULL, "", (s->conv == 'D') ? 'f' : s->conv);
    LOG_PRINT_SPEC(o, spec, s, d);  // printf-style floats: the only snprintf use
}

static bool arg_to_double(const log_arg_t *a, double *d) {
    switch (a->type) {
        case LOG_ARG_DOUBLE: *d = a->v.d; return true;
        case LOG_ARG_INT:    *d = (double)sign_extend(arg_raw(a), arg_bits(a)); return true;
        case LOG_ARG_UINT:
        case LOG_ARG_BOOL:   *d = (double)a->v.u; return true;
        default:             return false;
    }
}

/**
 * @brief Format one parsed conversion, consuming its argument
 */
static void emit_conversion(log_out_t *o, const log_spec_t *s, log_arg_iter_t *it,
                            unsigned int options) {
    const log_arg_t *a = next_arg(it);
    if (!a) {
        out_cstr(o, "<?>");
        return;
    }

    switch (s->conv) {
        case 'd':
        case 'i':
            if (arg_is_integer(a)) { emit_signed(o, s, a); return; }
            break;

        case 'u':
        case 'o':
        case 'x':
        case 'X':
            if (arg_is_integer(a)) { emit_unsigned(o, s, a, options); return; }
            break;

        case 'b':
        case 'B':
            if (arg_is_integer(a)) { emit_binary(o, s, a); return; }
            break;

        case 'c':
            if (arg_is_integer(a)) {
                char c = (char)(arg_raw(a) & 0xFFu);
                emit_padded(o, s, &c, 1);
                return;
            }
            break;

        case 's':
        case 'S':
        case 'P':
        case 'I':
            if (a->type == LOG_ARG_STRING) {
                emit_string(o, s, a->v.s ? a->v.s : "(null)");
                return;
            }
            if (a->type == LOG_ARG_IPV4) {
                char ip[16];
                format_ipv4(ip, a->v.u);
                emit_string(o, s, ip);
                return;
            }
            break;

        case 't':
        case 'T':
            if (arg_is_integer(a)) {
                bool truth = mask_bits(arg_raw(a), arg_bits(a)) != 0u;
                const char *text = (s->conv == 't') ? (truth ? "T" : "F")
                                                    : (truth ? "true" : "false");
                emit_string(o, s, text);
                return;
            }
            break;

        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
        case 'D': {
            double d;
            if (arg_to_double(a, &d)) { emit_double(o, s, d, options); return; }
            break;
        }

        case 'p': {
            const void *p = NULL;
            if (a->type == LOG_ARG_POINTER) {
                p = a->v.p;
            } else if (a->type == LOG_ARG_STRING) {
                p = (const void *)a->v.s;
            } else if (a->type == LOG_ARG_INT || a->type == LOG_ARG_UINT) {
                p = (const void *)(uintptr_t)arg_raw(a);
            } else {
                break;
            }
            emit_pointer(o, s, p);
            return;
        }

        default:  // 'n' and anything else that takes an argument
            break;
    }
    out_cstr(o, "<!>");
}

// Conversions that accept a length modifier; "%l" before anything else is ArduinoLog "long"
static const char k_length_convs[] = "diouxXbBnfFeEgGaA";
static const char k_known_convs[] = "diouxXbBcsSPIpfFeEgGaADtTn";

size_t log_format_values(char *buffer, size_t buffer_size, const char *format,
                         const unsigned char *kinds, const log_value_t *values,
                         size_t count, unsigned int options) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }
    buffer[0] = '\0';

    log_out_t out = { buffer, buffer_size, 0 };
    if (!format) {
        out_cstr(&out, "(null format)");
        return out.len;
    }

    log_arg_iter_t it;
    memset(&it, 0, sizeof(it));
    if (kinds && values) {
        it.kinds = kinds;
        it.values = values;
        it.count = count;
    }
    const char *p = format;

    while (*p) {
        if (*p != '%') {
            const char *q = strchr(p, '%');
            size_t n = q ? (size_t)(q - p) : strlen(p);
            out_str(&out, p, n);
            p += n;
            continue;
        }

        const char *start = p++;  // at '%'
        if (*p == '%') {
            out_str(&out, "%", 1);
            p++;
            continue;
        }

        log_spec_t s;
        memset(&s, 0, sizeof(s));

        // Flags
        while (*p && strchr("-+ #0", *p)) {
            add_flag(&s, *p++);
        }

        // Width
        if (*p == '*') {
            p++;
            const log_arg_t *a = next_arg(&it);
            long long w = (a && arg_is_integer(a)) ? sign_extend(arg_raw(a), arg_bits(a)) : 0;
            if (w < 0) {
                add_flag(&s, '-');
                w = -w;
            }
            s.has_width = true;
            s.width = (int)w;
        } else if (*p >= '0' && *p <= '9') {
            s.has_width = true;
            while (*p >= '0' && *p <= '9') {
                s.width = s.width * 10 + (*p++ - '0');
            }
        }

        // Precision
        if (*p == '.') {
            p++;
            s.has_prec = true;
            if (*p == '*') {
                p++;
                const log_arg_t *a = next_arg(&it);
                long long pr = (a && arg_is_integer(a)) ? sign_extend(arg_raw(a), arg_bits(a)) : 0;
                if (pr < 0) {
                    s.has_prec = false;  // negative precision: as if omitted
                } else {
                    s.prec = (int)pr;
                }
            } else {
                while (*p >= '0' && *p <= '9') {
                    s.prec = s.prec * 10 + (*p++ - '0');
                }
            }
        }

        // Length modifier
        if (p[0] == 'h' && p[1] == 'h') {
            strcpy(s.length, "hh");
            p += 2;
        } else if (p[0] == 'l' && p[1] == 'l') {
            strcpy(s.length, "ll");
            p += 2;
        } else if (*p && strchr("hljztL", *p)) {
            s.length[0] = *p++;
        }

        // A single length letter not followed by a conversion that takes it is
        // itself the conversion: ArduinoLog "%t" (boolean) versus printf "%td"
        if (s.length[0] != '\0' && s.length[1] == '\0' && s.length[0] != 'l' &&
            (*p == '\0' || !strchr(k_length_convs, *p))) {
            p--;
            s.length[0] = '\0';
        }

        // Conversion
        if (strcmp(s.length, "l") == 0 && (*p == '\0' || !strchr(k_length_convs, *p))) {
            s.conv = 'd';  // ArduinoLog "%l": long decimal, next character is literal
        } else if (*p == '\0') {
            out_cstr(&out, start);  // incomplete specification at the end
            break;
        } else {
            s.conv = *p++;
        }

        if (!strchr(k_known_convs, s.conv)) {
            out_str(&out, start, (size_t)(p - start));  // unknown: print it literally
            continue;
        }

        s.bare = (s.flags[0] == '\0') && !s.has_width && !s.has_prec && (s.length[0] == '\0');
        emit_conversion(&out, &s, &it, options);
    }

    return out.len;
}

// *************************************************************************
//  Logging Entry Point
// *************************************************************************

typedef struct {
    const char *format;
    const unsigned char *kinds;
    const log_value_t *values;
    size_t count;
    unsigned int options;
} log_args_ctx_t;

static size_t log_args_formatter(char *buffer, size_t buffer_size, void *context) {
    const log_args_ctx_t *ctx = (const log_args_ctx_t *)context;
    size_t len = log_format_values(buffer, buffer_size, ctx->format, ctx->kinds,
                                   ctx->values, ctx->count, ctx->options);
    if ((ctx->options & LOG_FORMAT_STRIP_EOL) && len > 0 && buffer[len - 1] == '\n') {
        len--;
        if (len > 0 && buffer[len - 1] == '\r') {
            len--;
        }
        buffer[len] = '\0';
    }
    return len;
}

void log_write_values(unsigned int level, const char *tag, const char *file, int line,
                      const char *format, const unsigned char *kinds,
                      const log_value_t *values, size_t count, unsigned int options) {
    log_args_ctx_t ctx = { format, kinds, values, count, options };
    log_write_cb(level, tag, file, line, log_args_formatter, &ctx);
}
