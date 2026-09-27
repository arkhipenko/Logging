/**
  ******************************************************************************
  * @file           : test_arduinolog.cpp
  * @brief          : ArduinoLog-compatible interface on the host (fake Arduino
  *                   layer): output layout, levels, prefix/suffix, argument
  *                   types, printf options and the worked examples of CHG-0006
  ******************************************************************************
  */

#include <ArduinoLog.h>
#include <stdio.h>
#include <string>

static int g_failures = 0;
static int g_checks = 0;

class CapturePrint : public Print {
  public:
    size_t write(uint8_t c) override { out += (char)c; return 1; }
    std::string take() { std::string s = out; out.clear(); return s; }
    std::string out;
};

static CapturePrint cap;

static void check(const std::string &got, const std::string &want, const char *what) {
    g_checks++;
    if (got != want) {
        printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got.c_str(), want.c_str());
        g_failures++;
    }
}

static void prefix(Print *p) { p->print("PFX "); }
static void suffix(Print *p) { p->print("<SFX>"); }

enum class Mode : uint8_t { Idle = 3 };
enum Plain { PLAIN_TWO = 2 };

#pragma pack(push, 1)
struct Packed { uint8_t a; uint32_t b; };
#pragma pack(pop)
struct Bits { unsigned x : 3; };

// ArduinoLog level names: upstream (0-6) and the ARDUINO_LOG_ prefixed form
static_assert(LOG_LEVEL_SILENT == 0 && LOG_LEVEL_FATAL == 1 && LOG_LEVEL_ERROR == 2 &&
              LOG_LEVEL_WARNING == 3 && LOG_LEVEL_NOTICE == 4 && LOG_LEVEL_TRACE == 5 &&
              LOG_LEVEL_VERBOSE == 6, "ArduinoLog level names");
static_assert(ARDUINO_LOG_LEVEL_WARNING == LOG_LEVEL_WARNING, "prefixed names");
#if defined(LOG_LEVEL_EMERG) || defined(LOG_INFO)
#error "syslog short names must be hidden when ArduinoLog.h is used"
#endif

int main() {
    Log.notice("before begin" CR);
    check(cap.take(), "", "nothing is logged before begin()");

    Log.begin(ARDUINO_LOG_LEVEL_VERBOSE, &cap);
    Log.setPrefix(prefix);

    // Layout and levels
    Log.trace("setupLogging()" CR);
    check(cap.take(), "PFX T: setupLogging()\n", "trace layout with prefix");
    Log.fatal("f" CR);    check(cap.take(), "PFX F: f\n", "fatal");
    Log.error("e" CR);    check(cap.take(), "PFX E: e\n", "error");
    Log.warning("w" CR);  check(cap.take(), "PFX W: w\n", "warning");
    Log.notice("n" CR);   check(cap.take(), "PFX N: n\n", "notice");
    Log.verbose("v" CR);  check(cap.take(), "PFX V: v\n", "verbose");
    Log.notice("no CR");  check(cap.take(), "PFX N: no CR\n", "message without CR is one line");
    Log.notice("crlf\r\n"); check(cap.take(), "PFX N: crlf\n", "CR LF stripped");
    Log.notice("two\n\n"); check(cap.take(), "PFX N: two\n\n", "only one line end stripped");

    // Worked examples (CHG-0006)
    Log.notice("v=%04x" CR, 26);                     check(cap.take(), "PFX N: v=001a\n", "%04x");
    Log.notice("m=0x%x" CR, 26);                     check(cap.take(), "PFX N: m=0x1a\n", "bare %x is printf");
    Log.notice("r=%X" CR, 26);                       check(cap.take(), "PFX N: r=0x1A\n", "bare %X keeps 0x");
    Log.notice("r=%04X" CR, 26);                     check(cap.take(), "PFX N: r=001A\n", "%04X");
    Log.notice("t=%lu ms" CR, 5000UL);               check(cap.take(), "PFX N: t=5000 ms\n", "%lu");
    Log.notice("k=%-8S|" CR, String("ab"));          check(cap.take(), "PFX N: k=ab      |\n", "%-8S");
    Log.notice("p=%.1F" CR, 3.14159f);               check(cap.take(), "PFX N: p=3.1\n", "%.1F");
    Log.notice("ok=%t" CR, true);                    check(cap.take(), "PFX N: ok=T\n", "%t");

    // ArduinoLog specifiers as used in smartcuffs
    Log.notice("param %S saved (%F). rc = %d" CR, String("KEY"), 1.5f, 0);
    check(cap.take(), "PFX N: param KEY saved (1.50). rc = 0\n", "%S %F %d");
    Log.notice("heap = %l" CR, 123456L);             check(cap.take(), "PFX N: heap = 123456\n", "%l");
    Log.notice("%T/%t" CR, false, 1);                check(cap.take(), "PFX N: false/T\n", "%T %t");
    Log.notice("%u" CR, 4000000000u);                check(cap.take(), "PFX N: 4000000000\n", "%u");
    Log.notice("%c%c" CR, 'o', 'k');                 check(cap.take(), "PFX N: ok\n", "%c");
    Log.notice("%s|%S" CR, "c", String("s"));        check(cap.take(), "PFX N: c|s\n", "%s and %S");
    Log.notice("100%%" CR);                          check(cap.take(), "PFX N: 100%\n", "%%");
    Log.notice("%b %B" CR, 5, 5);                    check(cap.take(), "PFX N: 101 0b101\n", "%b %B");
    Log.notice("%I" CR, IPAddress(192, 168, 1, 10)); check(cap.take(), "PFX N: 192.168.1.10\n", "%I");

    // Argument types
    Log.notice("%S" CR, String("x") + String("y"));  check(cap.take(), "PFX N: xy\n", "String + String");
    Log.notice("%s" CR, String("str"));              check(cap.take(), "PFX N: str\n", "%s with String");
    Log.notice("%d %d" CR, Mode::Idle, PLAIN_TWO);   check(cap.take(), "PFX N: 3 2\n", "enums");
    Log.notice("%x" CR, (uint8_t)0xAB);              check(cap.take(), "PFX N: ab\n", "uint8_t");
    Log.notice("%d" CR, (int16_t)-5);                check(cap.take(), "PFX N: -5\n", "int16_t");
    Log.notice("%lld|%d" CR, -5000000000LL, 5000000000LL);
    check(cap.take(), "PFX N: -5000000000|5000000000\n", "64-bit");
    // volatile arguments (ISR-shared counters): read by value, as ArduinoLog did
    volatile unsigned long vul = 4000000000UL;
    volatile long long vll = -5000000000LL;
    volatile int vi = -7;
    volatile bool vb = true;
    volatile float vf = 2.5f;
    volatile Mode vm = Mode::Idle;
    char text[] = "vp";
    char *volatile vp = text;
    Log.notice("%lu %lld %d %t %F %d %s" CR, vul, vll, vi, vb, vf, vm, vp);
    check(cap.take(), "PFX N: 4000000000 -5000000000 -7 T 2.50 3 vp\n", "volatile arguments");
    Packed pk; pk.a = 1; pk.b = 77;
    Log.notice("%u" CR, pk.b);                       check(cap.take(), "PFX N: 77\n", "packed field");
    Bits bt; bt.x = 5;
    Log.notice("%u" CR, bt.x);                       check(cap.take(), "PFX N: 5\n", "bit-field");
    char buf[] = "arr";
    Log.notice("%s" CR, buf);                        check(cap.take(), "PFX N: arr\n", "char array");
    Log.notice("%.3f|%8.2f|" CR, 2.0 / 3.0, -1.5);   check(cap.take(), "PFX N: 0.667|   -1.50|\n", "double options");
    Log.notice("%d %d" CR, 1);                       check(cap.take(), "PFX N: 1 <?>\n", "missing argument");
    Log.notice("%d" CR, "text");                     check(cap.take(), "PFX N: <!>\n", "mismatched argument");
    Log.notice(F("flash %d" CR), 5);                 check(cap.take(), "PFX N: flash 5\n", "F() format");
    Log.notice(String("string fmt %d" CR), 7);       check(cap.take(), "PFX N: string fmt 7\n", "String format");

    // Settings
    Log.setShowLevel(false);
    Log.notice("x" CR);                              check(cap.take(), "PFX x\n", "setShowLevel(false)");
    Log.setShowLevel(true);
    Log.setSuffix(suffix);
    Log.notice("x" CR);                              check(cap.take(), "PFX N: x\n<SFX>", "suffix after line end");
    Log.setSuffix(NULL);
    Log.setLevel(ARDUINO_LOG_LEVEL_WARNING);
    Log.notice("hidden" CR);
    Log.warning("shown" CR);                         check(cap.take(), "PFX W: shown\n", "setLevel filters");
    check(Log.getLevel() == ARDUINO_LOG_LEVEL_WARNING ? "ok" : "bad", "ok", "getLevel");
    Log.setLevel(LOG_LEVEL_NOTICE);                  // upstream name, value 4
    Log.trace("hidden" CR);
    Log.notice("upstream names" CR);                 check(cap.take(), "PFX N: upstream names\n", "LOG_LEVEL_NOTICE is ArduinoLog NOTICE");
    Log.setLevel(ARDUINO_LOG_LEVEL_SILENT);
    Log.fatal("silent" CR);                          check(cap.take(), "", "SILENT");
    Log.setLevel(99);
    check(Log.getLevel() == ARDUINO_LOG_LEVEL_VERBOSE ? "ok" : "bad", "ok", "setLevel clamps");
    CapturePrint other;
    Log.setOutput(&other);
    Log.notice("moved" CR);
    check(other.take(), "PFX N: moved\n", "setOutput");
    check(cap.take(), "", "old output unused");

    printf("%s %d ArduinoLog checks, %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
