/* Freestanding aarch64-linux stub for booting self-host-compiled ELF
 * relocatables under `qemu-system-aarch64 -M virt -kernel <image>`.
 *
 * The compiler's ELF path emits a relocatable whose undefined symbols are
 * the libc surface its emitted runtime calls (see ngen_obj.zan): malloc/
 * calloc/realloc/free, memcpy/memmove, strlen/strcmp, printf/sprintf/
 * snprintf/puts, exit. This stub provides them against a bump heap and
 * routes all output through ARM semihosting (SYS_WRITE0), so the image
 * needs no kernel, libc, or firmware:
 *
 *   ld.lld -m elf64laarch64 -static -nostdlib -T stub.ld fixture.o stub.o
 *   qemu-system-aarch64 -M virt -cpu max -nographic -semihosting-config \
 *       enable=on,target=native -kernel image
 *
 * Mini-printf covers the specs the emitted runtime and interpolation
 * mapper produce for integer/string programs: %s %c %d %i %u %x %p and
 * the l/ll lengths with '0'-pad width. Float specs (%f %g %e) abort the
 * boot loudly -- double rendering through bare metal is future work, so
 * boot fixtures must stay double-free.
 */

typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;

/* ---- semihosting ---- */

static void semi_syscall(long n, void *arg) {
    register long w0 __asm__("x0") = n;
    register void *x1 __asm__("x1") = arg;
    __asm__ volatile("hlt #0xF000" : "+r"(w0) : "r"(x1) : "memory");
}

static void semi_write0(const char *s) { semi_syscall(0x04, (void *)s); }

__attribute__((noreturn)) static void semi_exit(long code) {
    /* SYS_EXIT_EXTENDED: parameter block {reason, subcode}. */
    static volatile long blk[2];
    blk[0] = 0x20026; /* ADP_Stopped_ApplicationExit */
    blk[1] = code;
    semi_syscall(0x20, (void *)blk);
    for (;;) { }
}

/* ---- output buffering: batch chars so each WRITE0 is one line ---- */

static char obuf[4096];
static int olen;

static void oflush(void) {
    if (olen > 0) {
        obuf[olen] = 0;
        semi_write0(obuf);
        olen = 0;
    }
}

static void oputc(char c) {
    if (olen >= (int)sizeof(obuf) - 1) { oflush(); }
    obuf[olen++] = c;
}

static void oputs(const char *s) {
    while (*s != 0) {
        if (*s == '\n') { oputc('\n'); oflush(); s = s + 1; continue; }
        oputc(*s);
        s = s + 1;
    }
}

/* ---- bump heap (free is a no-op; arenas cover proof fixtures) ---- */

static char heap[96u * 1024u * 1024u] __attribute__((aligned(16)));
static u64 heap_at;

void *malloc(u64 n) {
    n = (n + 15u) & ~15ul;
    if (heap_at + n > sizeof(heap)) {
        oputs("stub: out of heap\n");
        oflush();
        semi_exit(3);
    }
    void *p = heap + heap_at;
    heap_at = heap_at + n;
    return p;
}

void *calloc(u64 n, u64 sz) {
    char *p = malloc(n * sz);
    for (u64 i = 0; i < n * sz; i = i + 1) { p[i] = 0; }
    return p;
}

void free(void *p) { (void)p; }

void *realloc(void *p, u64 n) {
    if (p == 0) { return malloc(n); }
    /* Old size is unknown; bump-allocate fresh and copy n bytes from the
     * old block. Blocks live in one monotonic arena, so reading n bytes
     * from the old block is safe unless it was the last allocation with
     * n past the arena end -- malloc above would have aborted first. */
    void *q = malloc(n);
    for (u64 i = 0; i < n; i = i + 1) { ((char *)q)[i] = ((char *)p)[i]; }
    return q;
}

/* ---- string/memory ---- */

void *memcpy(void *d, const void *s, u64 n) {
    char *dd = d;
    const char *ss = s;
    for (u64 i = 0; i < n; i = i + 1) { dd[i] = ss[i]; }
    return d;
}

void *memmove(void *d, const void *s, u64 n) {
    char *dd = d;
    const char *ss = s;
    if (dd < ss) {
        for (u64 i = 0; i < n; i = i + 1) { dd[i] = ss[i]; }
    } else {
        for (u64 i = n; i > 0; i = i - 1) { dd[i - 1] = ss[i - 1]; }
    }
    return d;
}

u64 strlen(const char *s) {
    u64 n = 0;
    while (s[n] != 0) { n = n + 1; }
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a != 0 && *a == *b) { a = a + 1; b = b + 1; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* ---- mini printf (integer/string specs only) ---- */

static char *unum(char *p, u64 v, u32 base, int upper) {
    char tmp[24];
    int n = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) { tmp[n++] = '0'; }
    while (v != 0) { tmp[n++] = dig[v % base]; v = v / base; }
    while (n > 0) { *p++ = tmp[--n]; }
    return p;
}

static void emit_num(const char **pf, int width, int zero, char *body,
                     char *end) {
    const char *f = *pf;
    char buf[32];
    char *b = body;
    int len = 0;
    while (b < end && *b != 0) { len = len + 1; b = b + 1; }
    while (len < width) { *b++ = zero ? '0' : ' '; len = len + 1; }
    *b = 0;
    /* zero-pad wants digits first; space-pad wants them last */
    if (zero) {
        int digits = 0;
        const char *q = body;
        while (*q != 0) { digits = digits + 1; q = q + 1; }
        int pad = width - digits;
        if (pad > 0) {
            for (int i = digits; i >= 0; i = i - 1) { buf[i + pad] = body[i]; }
            for (int i = 0; i < pad; i = i + 1) { buf[i] = '0'; }
            for (int i = 0; i < width; i = i + 1) { *b-- = 0; }
            for (int i = 0; i < width; i = i + 1) { *b++ = buf[i]; }
            *b = 0;
        }
    }
    oputs(body);
    *pf = f;
}

static void mini_vprintf(const char *f, __builtin_va_list ap) {
    char body[40];
    while (*f != 0) {
        if (*f != '%') { oputc(*f); f = f + 1; continue; }
        f = f + 1;
        if (*f == '%') { oputc('%'); f = f + 1; continue; }
        int zero = 0;
        int width = 0;
        if (*f == '0') { zero = 1; f = f + 1; }
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f = f + 1;
        }
        int islong = 0;
        while (*f == 'l') { islong = islong + 1; f = f + 1; }
        char spec = *f;
        if (spec == 0) { break; }
        f = f + 1;
        char *b = body;
        switch (spec) {
        case 's':
            oputs(__builtin_va_arg(ap, const char *));
            break;
        case 'c':
            *b++ = (char)__builtin_va_arg(ap, int);
            *b = 0;
            oputs(body);
            break;
        case 'd':
        case 'i': {
            i64 v = islong ? __builtin_va_arg(ap, i64)
                           : (i64)__builtin_va_arg(ap, int);
            if (v < 0) {
                *b++ = '-';
                v = -v;
            }
            b = unum(b, (u64)v, 10, 0);
            *b = 0;
            emit_num(&f, width, zero, body, b);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            u64 v = islong ? __builtin_va_arg(ap, u64)
                           : (u64)__builtin_va_arg(ap, u32);
            b = unum(b, v, spec == 'x' || spec == 'X' ? 16 : 10,
                     spec == 'X');
            *b = 0;
            emit_num(&f, width, zero, body, b);
            break;
        }
        case 'p': {
            void *v = __builtin_va_arg(ap, void *);
            *b++ = '0';
            *b++ = 'x';
            b = unum(b, (u64)v, 16, 0);
            *b = 0;
            oputs(body);
            break;
        }
        default:
            oputs("stub: unsupported printf spec %");
            oputc(spec);
            oputc('\n');
            oflush();
            semi_exit(4);
        }
    }
    oflush();
}

int printf(const char *f, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, f);
    mini_vprintf(f, ap);
    __builtin_va_end(ap);
    return 0;
}

int sprintf(char *dst, const char *f, ...) {
    /* Integer-only rendering straight into dst. */
    __builtin_va_list ap;
    __builtin_va_start(ap, f);
    char *d = dst;
    while (*f != 0) {
        if (*f != '%') { *d++ = *f; f = f + 1; continue; }
        f = f + 1;
        if (*f == '%') { *d++ = '%'; f = f + 1; continue; }
        int zero = 0;
        int width = 0;
        if (*f == '0') { zero = 1; f = f + 1; }
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f = f + 1;
        }
        int islong = 0;
        while (*f == 'l') { islong = islong + 1; f = f + 1; }
        char spec = *f;
        f = f + 1;
        if (spec == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            while (*s != 0) { *d++ = *s++; }
        } else if (spec == 'd' || spec == 'i') {
            i64 v = islong ? __builtin_va_arg(ap, i64)
                           : (i64)__builtin_va_arg(ap, int);
            char tmp[24];
            int n = 0;
            if (v < 0) { *d++ = '-'; v = -v; }
            if (v == 0) { tmp[n++] = '0'; }
            while (v != 0) { tmp[n++] = (char)('0' + v % 10); v = v / 10; }
            while (n < width) { tmp[n++] = zero ? '0' : ' '; }
            while (n > 0) { *d++ = tmp[--n]; }
        } else if (spec == 'u') {
            u64 v = islong ? __builtin_va_arg(ap, u64)
                           : (u64)__builtin_va_arg(ap, u32);
            char tmp[24];
            int n = 0;
            if (v == 0) { tmp[n++] = '0'; }
            while (v != 0) { tmp[n++] = (char)('0' + v % 10); v = v / 10; }
            while (n < width) { tmp[n++] = zero ? '0' : ' '; }
            while (n > 0) { *d++ = tmp[--n]; }
        } else {
            *d++ = '?';
        }
    }
    *d = 0;
    __builtin_va_end(ap);
    return (int)(d - dst);
}

int snprintf(char *dst, u64 cap, const char *f, ...) {
    /* Same integer-only subset; result truncated at cap-1. */
    __builtin_va_list ap;
    __builtin_va_start(ap, f);
    char big[256];
    char *d = big;
    char *stop = big + sizeof(big) - 1;
    while (*f != 0 && d < stop) {
        if (*f != '%') { *d++ = *f; f = f + 1; continue; }
        f = f + 1;
        if (*f == '%') { *d++ = '%'; f = f + 1; continue; }
        int zero = 0;
        int width = 0;
        if (*f == '0') { zero = 1; f = f + 1; }
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f = f + 1;
        }
        int islong = 0;
        while (*f == 'l') { islong = islong + 1; f = f + 1; }
        char spec = *f;
        f = f + 1;
        if (spec == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            while (*s != 0 && d < stop) { *d++ = *s++; }
        } else if (spec == 'd' || spec == 'i') {
            i64 v = islong ? __builtin_va_arg(ap, i64)
                           : (i64)__builtin_va_arg(ap, int);
            char tmp[24];
            int n = 0;
            if (v < 0) { *d++ = '-'; v = -v; }
            if (v == 0) { tmp[n++] = '0'; }
            while (v != 0) { tmp[n++] = (char)('0' + v % 10); v = v / 10; }
            while (n < width && d < stop) { tmp[n++] = zero ? '0' : ' '; }
            while (n > 0 && d < stop) { *d++ = tmp[--n]; }
        } else if (spec == 'u') {
            u64 v = islong ? __builtin_va_arg(ap, u64)
                           : (u64)__builtin_va_arg(ap, u32);
            char tmp[24];
            int n = 0;
            if (v == 0) { tmp[n++] = '0'; }
            while (v != 0) { tmp[n++] = (char)('0' + v % 10); v = v / 10; }
            while (n < width && d < stop) { tmp[n++] = zero ? '0' : ' '; }
            while (n > 0 && d < stop) { *d++ = tmp[--n]; }
        } else {
            *d++ = '?';
        }
    }
    *d = 0;
    __builtin_va_end(ap);
    for (u64 i = 0; i < cap && big[i] != 0; i = i + 1) { dst[i] = big[i]; }
    if (cap > 0) {
        u64 n = strlen(big);
        dst[n < cap - 1 ? n : cap - 1] = 0;
    }
    return (int)strlen(big);
}

int puts(const char *s) {
    oputs(s);
    oputc('\n');
    oflush();
    return 0;
}

void exit(int code) {
    oflush();
    semi_exit(code);
}

/* ---- referenced-but-unreached host ABI surface ----
 *
 * The ELF runtime blob references the full host ABI (file I/O, directory
 * listing, crypto, mach) in functions the boot fixtures never execute,
 * but the linker keeps every section, so all symbols must resolve. Each
 * stub below ABORTS with its name: a boot that prints its fixture output
 * proves the executed path reached none of them; an abort names exactly
 * the function that needs a real implementation next.
 */

void *stderr = 0;
unsigned int mach_task_self_ = 0;

#define UNREACHED(name) \
    void name(void) { \
        oputs("stub: reached unreached host fn " #name "\n"); \
        oflush(); \
        semi_exit(5); \
    }

UNREACHED(access)
UNREACHED(atoi)
UNREACHED(chmod)
UNREACHED(closedir)
UNREACHED(dlerror)
UNREACHED(getcwd)
UNREACHED(glob)
UNREACHED(globfree)
UNREACHED(mach_vm_read_overwrite)
UNREACHED(opendir)
UNREACHED(readdir)
UNREACHED(realpath)
UNREACHED(rmdir)
UNREACHED(stat)
UNREACHED(strtod)
UNREACHED(strtoll)
UNREACHED(unlink)
UNREACHED(usleep)
/* The soft guard (ngen_guard.zan) reports OOB writes through the host
 * file/time surface and is written to degrade when that surface fails:
 * fopen NULL skips the log entry, localtime_r NULL omits the timestamp,
 * mkdir/readlink failures fall back. Bare metal has no filesystem, so
 * hand the guard exactly the failure values it already handles -- the
 * fixture's stdout path is untouched. */
void *fopen(const char *p, const char *m) { (void)p; (void)m; return 0; }
int fclose(void *f) { (void)f; return 0; }
int fflush(void *f) { (void)f; return 0; }
int fseek(void *f, long o, int w) { (void)f; (void)o; (void)w; return -1; }
long ftell(void *f) { (void)f; return -1; }
u64 fread(void *b, u64 sz, u64 n, void *f) {
    (void)b; (void)sz; (void)n; (void)f; return 0;
}
u64 fwrite(const void *b, u64 sz, u64 n, void *f) {
    (void)b; (void)sz; (void)n; (void)f; return 0;
}
int fprintf(void *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
int mkdir(const char *p, int mode) { (void)p; (void)mode; return -1; }
long time(long *t) { if (t != 0) { *t = 0; } return 0; }
void *localtime_r(const long *timep, void *result) {
    (void)timep; (void)result; return 0;
}
u64 strftime(char *s, u64 max, const char *f, const void *tm) {
    (void)f; (void)tm;
    if (max > 0) { s[0] = 0; }
    return 0;
}
long readlink(const char *p, char *b, u64 s) {
    (void)p; (void)b; (void)s; return -1;
}

/* Single-threaded bare metal: an uncontended lock is exactly a no-op,
 * and the emitted runtime's interning path really calls these. */
int os_unfair_lock_lock(void *lock) { (void)lock; return 0; }
int os_unfair_lock_unlock(void *lock) { (void)lock; return 0; }

UNREACHED(CC_MD5)
UNREACHED(CC_SHA1)
UNREACHED(CC_SHA256)
UNREACHED(CCHmac)
UNREACHED(zan_sha512)
UNREACHED(_NSGetExecutablePath)
UNREACHED(pthread_threadid_np)

/* Plausibly reachable even in minimal programs: real (small) behavior. */
const char *getenv(const char *name) {
    (void)name;
    return 0;
}

char *strchr(const char *s, int c) {
    do {
        if (*s == (char)c) { return (char *)s; }
    } while (*s++ != 0);
    return 0;
}

char *strrchr(const char *s, int c) {
    const char *last = 0;
    do {
        if (*s == (char)c) { last = s; }
    } while (*s++ != 0);
    return (char *)last;
}

char *strstr(const char *h, const char *n) {
    if (*n == 0) { return (char *)h; }
    for (; *h != 0; h = h + 1) {
        const char *a = h;
        const char *b = n;
        while (*a != 0 && *a == *b) { a = a + 1; b = b + 1; }
        if (*b == 0) { return (char *)h; }
    }
    return 0;
}

void arc4random_buf(void *buf, u64 n) {
    u64 x = 0x9E3779B97F4A7C15ul;
    unsigned char *p = buf;
    for (u64 i = 0; i < n; i = i + 1) {
        x = x * 6364136223846793005ul + 1442695040888963407ul;
        p[i] = (unsigned char)(x >> 33);
    }
}

unsigned long pthread_self(void) { return 0; }

/* clock_gettime: monotonic-ish counter, enough for unreached-time paths. */
struct ts_dummy { long sec; long nsec; };
int clock_gettime(int clk, struct ts_dummy *ts) {
    (void)clk;
    static long ticks;
    ticks = ticks + 1;
    ts->sec = ticks;
    ts->nsec = 0;
    return 0;
}

/* ---- boot ---- */

int main(int argc, char **argv);

/* Global (not static): only the naked _start assembly references it, and
 * LLVM discards unreferenced statics before the assembler ever sees the
 * relocations. */
char stack_area[1u << 20] __attribute__((aligned(16), used));

extern char __bss_start[];
extern char __bss_end[];

/* `used` + external linkage: only the naked _start assembly references
 * boot and stack_area, so LLVM would discard them as unreferenced. */
/* The emitted runtime's main(argc, argv) stashes both into host_argc/
 * host_argv; Environment.ArgCount() reports argc-1. Hand it the host
 * convention for "no arguments": argc=1, argv={NULL}. */
char *stub_argv[1] = { 0 };

__attribute__((used)) void boot(void) {
    for (char *p = __bss_start; p < __bss_end; p = p + 1) { *p = 0; }
    int rc = main(1, stub_argv);
    oflush();
    semi_exit((long)rc);
}

__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile(
        /* Bare metal boots with FP/SIMD trapped (CPACR_EL1.FPEN=0); the
         * emitted runtime uses NEON saves (stp q0, q1), so enable it. */
        "mrs x0, cpacr_el1\n"
        "orr x0, x0, #(3 << 20)\n"
        "msr cpacr_el1, x0\n"
        "isb\n"
        "adrp x0, stack_area\n"
        "add x0, x0, :lo12:stack_area\n"
        "mov x9, #0x100000\n"          /* 1 MiB stack */
        "add sp, x0, x9\n"
        "bl boot\n"
        "b .\n");
}
