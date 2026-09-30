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

static long semi_syscall(long n, void *arg) {
    register long w0 __asm__("x0") = n;
    register void *x1 __asm__("x1") = arg;
    __asm__ volatile("hlt #0xF000" : "+r"(w0) : "r"(x1) : "memory");
    return w0;
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

/* ---- mini printf ---- */

/* Exact C %e/%f/%g engine, defined after the strtod bignum block (it
 * shares that machinery). The emitted runtime's double formatting calls
 * snprintf(buf, 40, "%.17g") -- libc answers on the host lanes; bare
 * metal answers here, byte-faithful to C printf for finite values. */
static int zan_fmt_double(char *out, double v, int prec, char style);

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
    /* zero-pad wants digits first; space-pad wants them last. A leading
     * sign stays ahead of the zeros (C %+03d of 5 is "+05", not "0+5"). */
    if (zero) {
        int digits = 0;
        const char *q = body;
        while (*q != 0) { digits = digits + 1; q = q + 1; }
        int pad = width - digits;
        if (pad > 0) {
            int sign = (body[0] == '-' || body[0] == '+') ? 1 : 0;
            for (int i = digits; i >= sign; i = i - 1) {
                buf[i + pad] = body[i];
            }
            for (int i = 0; i < sign; i = i + 1) { buf[i] = body[i]; }
            for (int i = 0; i < pad; i = i + 1) { buf[sign + i] = '0'; }
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
        int plus = 0;
        int width = 0;
        for (;;) {
            if (*f == '0') { zero = 1; f = f + 1; }
            else if (*f == '+') { plus = 1; f = f + 1; }
            else if (*f == '-') { f = f + 1; }
            else if (*f == ' ') { f = f + 1; }
            else { break; }
        }
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f = f + 1;
        }
        int prec = -1;
        if (*f == '.') {
            f = f + 1;
            if (*f == '*') {
                prec = __builtin_va_arg(ap, int);
                f = f + 1;
            } else {
                prec = 0;
                while (*f >= '0' && *f <= '9') {
                    prec = prec * 10 + (*f - '0');
                    f = f + 1;
                }
            }
        }
        int islong = 0;
        while (*f == 'l') { islong = islong + 1; f = f + 1; }
        char spec = *f;
        if (spec == 0) { break; }
        f = f + 1;
        char *b = body;
        switch (spec) {
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': {
            double v = __builtin_va_arg(ap, double);
            char tmp[1600];
            zan_fmt_double(tmp, v, prec, spec);
            oputs(tmp);
            break;
        }
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
            } else if (plus) {
                *b++ = '+';
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

/* Shared buffered walker for sprintf/snprintf. Returns the untruncated
 * length; dst is always NUL-terminated when cap allows. */
static int zan_vsnfmt(char *dst, u64 cap, const char *f,
                      __builtin_va_list ap) {
    char big[1600];
    char *d = big;
    char *stop = big + sizeof(big) - 1;
    while (*f != 0 && d < stop) {
        if (*f != '%') { *d++ = *f; f = f + 1; continue; }
        f = f + 1;
        if (*f == '%') { *d++ = '%'; f = f + 1; continue; }
        int zero = 0;
        int plus = 0;
        int width = 0;
        for (;;) {
            if (*f == '0') { zero = 1; f = f + 1; }
            else if (*f == '+') { plus = 1; f = f + 1; }
            else if (*f == '-') { f = f + 1; }
            else if (*f == ' ') { f = f + 1; }
            else { break; }
        }
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f = f + 1;
        }
        int prec = -1;
        if (*f == '.') {
            f = f + 1;
            if (*f == '*') {
                prec = __builtin_va_arg(ap, int);
                f = f + 1;
            } else {
                prec = 0;
                while (*f >= '0' && *f <= '9') {
                    prec = prec * 10 + (*f - '0');
                    f = f + 1;
                }
            }
        }
        int islong = 0;
        while (*f == 'l') { islong = islong + 1; f = f + 1; }
        char spec = *f;
        f = f + 1;
        if (spec == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            while (*s != 0 && d < stop) { *d++ = *s++; }
        } else if (spec == 'e' || spec == 'E' || spec == 'f' ||
                   spec == 'F' || spec == 'g' || spec == 'G') {
            double v = __builtin_va_arg(ap, double);
            char tmp[1600];
            zan_fmt_double(tmp, v, prec, spec);
            for (const char *t = tmp; *t != 0 && d < stop; t = t + 1) {
                *d++ = *t;
            }
        } else if (spec == 'd' || spec == 'i') {
            i64 v = islong ? __builtin_va_arg(ap, i64)
                           : (i64)__builtin_va_arg(ap, int);
            char tmp[24];
            int n = 0;
            int sign = 0;
            if (v < 0) { *d++ = '-'; v = -v; sign = 1; }
            else if (plus) { *d++ = '+'; sign = 1; }
            if (v == 0) { tmp[n++] = '0'; }
            while (v != 0) { tmp[n++] = (char)('0' + v % 10); v = v / 10; }
            {
                int w = width - (sign != 0 ? 1 : 0);
                while (n < w) { tmp[n++] = zero ? '0' : ' '; }
            }
            while (n > 0 && d < stop) { *d++ = tmp[--n]; }
        } else if (spec == 'u') {
            u64 v = islong ? __builtin_va_arg(ap, u64)
                           : (u64)__builtin_va_arg(ap, u32);
            char tmp[24];
            int n = 0;
            if (v == 0) { tmp[n++] = '0'; }
            while (v != 0) { tmp[n++] = (char)('0' + v % 10); v = v / 10; }
            while (n < width) { tmp[n++] = zero ? '0' : ' '; }
            while (n > 0 && d < stop) { *d++ = tmp[--n]; }
        } else if (spec == 'x' || spec == 'X') {
            u64 v = islong ? __builtin_va_arg(ap, u64)
                           : (u64)__builtin_va_arg(ap, u32);
            d = unum(d, v, 16, spec == 'X');
        } else if (spec == 'c') {
            *d++ = (char)__builtin_va_arg(ap, int);
        } else {
            *d++ = '?';
        }
    }
    *d = 0;
    {
        int total = (int)(d - big);
        u64 i = 0;
        for (; i + 1 < cap && big[i] != 0; i = i + 1) { dst[i] = big[i]; }
        if (cap > 0) { dst[i] = 0; }
        return total;
    }
}

int sprintf(char *dst, const char *f, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, f);
    int r = zan_vsnfmt(dst, (u64)-2, f, ap);
    __builtin_va_end(ap);
    return r;
}

int snprintf(char *dst, u64 cap, const char *f, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, f);
    int r = zan_vsnfmt(dst, cap, f, ap);
    __builtin_va_end(ap);
    return r;
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
UNREACHED(unlink)
UNREACHED(usleep)
/* The soft guard (ngen_guard.zan) reports OOB writes through the host
 * file/time surface and is written to degrade when that surface fails:
 * fopen NULL skips the log entry, localtime_r NULL omits the timestamp,
 * mkdir/readlink failures fall back. Bare metal has no filesystem, so
 * hand the guard exactly the failure values it already handles -- the
 * fixture's stdout path is untouched. */
/* ---- real file ABI over semihosting ----
 *
 * SYS_OPEN(0x01)/CLOSE/WRITE(0x05)/READ(0x06)/SEEK(0x0A)/FLEN(0x0C)/
 * REMOVE(0x0E) against the qemu host filesystem (target=native). FILE*
 * are slots in a fixed table; ftell is bookkeeping (semihosting has no
 * tell), SYS_SEEK/FLEN back fseek. tmpfile creates "zan_tmp_N.tmp" in
 * the qemu cwd and SYS_REMOVEs it at fclose. */
#define SH_OPEN 0x01
#define SH_CLOSE 0x02
#define SH_WRITE 0x05
#define SH_READ 0x06
#define SH_SEEK 0x0A
#define SH_FLEN 0x0C
#define SH_REMOVE 0x0E

typedef struct { int sh; long pos; int tmp; } shfile;
static shfile sh_files[8];
static int sh_tmp_counter;

static int sh_mode(const char *m) {
    int base = 0, b = 0, plus = 0;
    for (; *m; m = m + 1) {
        if (*m == 'r') { base = 0; }
        else if (*m == 'w') { base = 4; }
        else if (*m == 'a') { base = 8; }
        else if (*m == 'b') { b = 1; }
        else if (*m == '+') { plus = 1; }
    }
    return base + b * 1 + plus * 2;
}

void *fopen(const char *p, const char *m) {
    volatile long pb[3];
    int i;
    if (p == 0 || m == 0) { return 0; }
    for (i = 0; i < 8; i = i + 1) {
        if (sh_files[i].sh == 0) {
            pb[0] = (long)p;
            pb[1] = sh_mode(m);
            pb[2] = (long)strlen(p);
            long h = semi_syscall(SH_OPEN, (void *)pb);
            if (h <= 0) { return 0; }
            sh_files[i].sh = (int)h;
            sh_files[i].pos = 0;
            sh_files[i].tmp = 0;
            return &sh_files[i];
        }
    }
    return 0;
}

void *tmpfile(void) {
    char name[32];
    char *p = name;
    const char *lit = "zan_tmp_";
    int i;
    for (i = 0; lit[i] != 0; i = i + 1) { *p++ = lit[i]; }
    {
        u32 v = (u32)sh_tmp_counter;
        char d[12];
        int n = 0;
        sh_tmp_counter = sh_tmp_counter + 1;
        if (v == 0) { d[n++] = '0'; }
        while (v != 0) { d[n++] = (char)('0' + v % 10u); v = v / 10u; }
        while (n > 0) { *p++ = d[--n]; }
    }
    *p++ = '.';
    *p++ = 't';
    *p++ = 'm';
    *p++ = 'p';
    *p = 0;
    shfile *f = (shfile *)fopen(name, "w+");
    if (f != 0) { f->tmp = 1; }
    return f;
}

int fclose(void *f) {
    shfile *sf = (shfile *)f;
    volatile long pb[1];
    if (sf == 0 || sf->sh == 0) { return -1; }
    pb[0] = sf->sh;
    semi_syscall(SH_CLOSE, (void *)pb);
    if (sf->tmp) { semi_syscall(SH_REMOVE, (void *)pb); }
    sf->sh = 0;
    return 0;
}

int fflush(void *f) { (void)f; return 0; }

u64 fread(void *b, u64 sz, u64 n, void *f) {
    shfile *sf = (shfile *)f;
    volatile long pb[3];
    u64 total = sz * n;
    long notread;
    if (sf == 0 || sf->sh == 0 || total == 0) { return 0; }
    pb[0] = sf->sh;
    pb[1] = (long)b;
    pb[2] = (long)total;
    notread = semi_syscall(SH_READ, (void *)pb);
    if (notread < 0) { notread = (long)total; }
    {
        u64 got = total - (u64)notread;
        sf->pos = sf->pos + (long)got;
        return sz != 0 ? got / sz : 0;
    }
}

u64 fwrite(const void *b, u64 sz, u64 n, void *f) {
    shfile *sf = (shfile *)f;
    volatile long pb[3];
    u64 total = sz * n;
    long notwritten;
    if (sf == 0 || sf->sh == 0 || total == 0) { return 0; }
    pb[0] = sf->sh;
    pb[1] = (long)b;
    pb[2] = (long)total;
    notwritten = semi_syscall(SH_WRITE, (void *)pb);
    if (notwritten < 0) { notwritten = (long)total; }
    {
        u64 put = total - (u64)notwritten;
        sf->pos = sf->pos + (long)put;
        return sz != 0 ? put / sz : 0;
    }
}

int fseek(void *f, long o, int w) {
    shfile *sf = (shfile *)f;
    volatile long pb[2];
    long target;
    if (sf == 0 || sf->sh == 0) { return -1; }
    if (w == 0) { target = o; }
    else if (w == 1) { target = sf->pos + o; }
    else {
        pb[0] = sf->sh;
        long len = semi_syscall(SH_FLEN, (void *)pb);
        target = (len < 0 ? 0 : len) + o;
    }
    if (target < 0) { return -1; }
    pb[0] = sf->sh;
    pb[1] = target;
    if (semi_syscall(SH_SEEK, (void *)pb) != 0) { return -1; }
    sf->pos = target;
    return 0;
}

long ftell(void *f) {
    shfile *sf = (shfile *)f;
    if (sf == 0 || sf->sh == 0) { return -1; }
    return sf->pos;
}

int fprintf(void *f, const char *fmt, ...) {
    shfile *sf = (shfile *)f;
    __builtin_va_list ap;
    char big[512];
    if (sf == 0 || sf->sh == 0) { return 0; }
    /* Integer/string subset through sprintf's walker, then one write. */
    __builtin_va_start(ap, fmt);
    {
        const char *p = fmt;
        char *d = big;
        char *stop = big + sizeof(big) - 1;
        while (*p != 0 && d < stop) {
            if (*p != '%') { *d++ = *p++; continue; }
            p = p + 1;
            if (*p == '%') { *d++ = '%'; p = p + 1; continue; }
            int islong = 0;
            while (*p == 'l') { islong = islong + 1; p = p + 1; }
            {
                char spec = *p;
                if (spec == 0) { break; }
                p = p + 1;
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
                    while (n > 0 && d < stop) { *d++ = tmp[--n]; }
                } else if (spec == 'u' || spec == 'x') {
                    u64 v = islong ? __builtin_va_arg(ap, u64)
                                   : (u64)__builtin_va_arg(ap, u32);
                    char tmp[24];
                    int n = 0;
                    if (v == 0) { tmp[n++] = '0'; }
                    while (v != 0) {
                        tmp[n++] = spec == 'x' ? "0123456789abcdef"[v % 16u]
                                               : (char)('0' + v % 10u);
                        v = v / (spec == 'x' ? 16u : 10u);
                    }
                    while (n > 0 && d < stop) { *d++ = tmp[--n]; }
                } else {
                    *d++ = '?';
                }
            }
        }
        *d = 0;
    }
    __builtin_va_end(ap);
    fwrite(big, 1, strlen(big), f);
    return 0;
}
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

/* ---- byte I/O on semihosting files ---- */

int fputc(int b, void *f) {
    unsigned char c = (unsigned char)b;
    if (fwrite(&c, 1, 1, f) != 1) { return -1; }
    return (int)c;
}

int fgetc(void *f) {
    unsigned char c;
    if (fread(&c, 1, 1, f) != 1) { return -1; }
    return (int)c;
}

void rewind(void *f) { fseek(f, 0, 0); }

/* ---- abs/labs/atol/atoi ---- */

u64 strtoll_full(const char *s, const char **end, int base);

int abs(int n) { return n < 0 ? -n : n; }
long labs(long n) { return n < 0 ? -n : n; }
int atoi(const char *s) { return (int)strtoll_full(s, 0, 10); }

/* ---- strtoll / strtod ----
 *
 * strtod is EXACT: the decimal mantissa is ingested into a base-2^32
 * bignum (C's minimum guarantee is 1100 significant digits), then
 *
 *   dexp >= 0:  value = (mant * 5^dexp) * 2^dexp          (multiply path)
 *   dexp <  0:  value = (mant / 5^-dexp) * 2^dexp         (division path)
 *
 * and rounded ONCE, to nearest-even, from the exact bit pattern -- the
 * division path uses restoring long division for a 56-bit prefix plus a
 * sticky bit (remainder nonzero, plus unconsumed N limbs), so there is
 * no pow() accuracy debt and no double rounding. Hex float literals
 * (0x1.8p+2) are exact by construction (shifts only).
 */

static int sh_isspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f'
        || c == '\r';
}

u64 strtoll_full(const char *s, const char **end, int base) {
    const char *p = s;
    int neg = 0;
    int any = 0;
    u64 acc = 0;
    u64 cap = (u64)-1;
    while (sh_isspace(*p)) { p = p + 1; }
    if (*p == '+') { p = p + 1; }
    else if (*p == '-') { neg = 1; p = p + 1; }
    if ((base == 16 || base == 0) && p[0] == '0'
        && (p[1] == 'x' || p[1] == 'X')) {
        p = p + 2;
        base = 16;
    }
    if (base == 0) { base = *p == '0' ? 8 : 10; }
    for (;;) {
        int d;
        if (*p >= '0' && *p <= '9') { d = *p - '0'; }
        else if (*p >= 'a' && *p <= 'z') { d = *p - 'a' + 10; }
        else if (*p >= 'A' && *p <= 'Z') { d = *p - 'A' + 10; }
        else { break; }
        if (d >= base) { break; }
        any = 1;
        if (acc > ((cap - (u64)d) / (u64)base)) {
            acc = cap; /* saturate; sign applied at the end */
        } else {
            acc = acc * (u64)base + (u64)d;
        }
        p = p + 1;
    }
    if (end != 0) { *(const char **)end = any ? p : s; }
    return neg ? (u64)(0 - acc) : acc;
}

long long strtoll(const char *s, char **e, int base) {
    return (long long)strtoll_full(s, (const char **)e, base);
}

long atol(const char *s) { return (long)strtoll_full(s, 0, 10); }

#define NBIG 480 /* mantissa limbs + pow5/division headroom */
typedef struct { int n; u32 l[NBIG]; } nbig;

static void nb_init(nbig *b) { b->n = 1; b->l[0] = 0; }

static void nb_trim(nbig *b) {
    while (b->n > 1 && b->l[b->n - 1] == 0) { b->n = b->n - 1; }
}

static int nb_is_zero(const nbig *b) {
    int i;
    for (i = 0; i < b->n; i = i + 1) { if (b->l[i] != 0) { return 0; } }
    return 1;
}

static int nb_bitlen(const nbig *b) {
    u32 t;
    int k = 0;
    if (nb_is_zero(b)) { return 0; }
    t = b->l[b->n - 1];
    while (t != 0) { k = k + 1; t = t >> 1; }
    return (b->n - 1) * 32 + k;
}

static void nb_mul_add(nbig *b, u32 m, u32 a) {
    u64 carry = a;
    int i;
    for (i = 0; i < b->n; i = i + 1) {
        u64 v = (u64)b->l[i] * (u64)m + carry;
        b->l[i] = (u32)v;
        carry = v >> 32;
    }
    while (carry != 0 && b->n < NBIG) {
        b->l[b->n++] = (u32)carry;
        carry = carry >> 32;
    }
}

static void nb_mul_big(nbig *r, const nbig *a, const nbig *b) {
    int i, j;
    int rn = a->n + b->n;
    if (rn > NBIG) { rn = NBIG; }
    for (i = 0; i < rn; i = i + 1) { r->l[i] = 0; }
    r->n = rn;
    for (i = 0; i < a->n; i = i + 1) {
        u64 carry = 0;
        if (a->l[i] == 0) { continue; }
        for (j = 0; j < b->n && i + j < rn; j = j + 1) {
            u64 v = (u64)a->l[i] * (u64)b->l[j] + (u64)r->l[i + j] + carry;
            r->l[i + j] = (u32)v;
            carry = v >> 32;
        }
        {
            int k = i + b->n;
            while (carry != 0 && k < rn) {
                u64 v = (u64)r->l[k] + carry;
                r->l[k] = (u32)v;
                carry = v >> 32;
                k = k + 1;
            }
        }
    }
    nb_trim(r);
}

static int nb_shr_sticky(nbig *b, int sh) {
    int sticky = 0;
    int lw = sh >> 5, rb = sh & 31;
    int i, n = b->n;
    if (sh <= 0) { return 0; }
    for (i = 0; i < n && i < lw; i = i + 1) {
        if (b->l[i] != 0) { sticky = 1; }
    }
    if (lw < n && rb != 0 && (b->l[lw] & ((1u << rb) - 1u)) != 0) { sticky = 1; }
    for (i = 0; i < n; i = i + 1) {
        int src = i + lw;
        u32 lo = src < n ? b->l[src] : 0;
        u32 hi = src + 1 < n ? b->l[src + 1] : 0;
        u32 v = rb != 0 ? ((lo >> rb) | (hi << (32 - rb))) : lo;
        b->l[i] = v;
    }
    nb_trim(b);
    return sticky;
}

/* 5^k cached monotonically (k only grows; smaller k recomputes from 1,
 * which is rare and cheap). */
static nbig p5_cache;
static int p5_k = -1;

static void pow5(int k, nbig *out) {
    int i;
    if (k <= p5_k) {
        nb_init(out);
        nb_mul_add(out, 1, 1);
        for (i = 0; i < k; i = i + 1) { nb_mul_add(out, 5, 0); }
        return;
    }
    if (p5_k < 0) { nb_init(&p5_cache); nb_mul_add(&p5_cache, 1, 1); p5_k = 0; }
    while (p5_k < k) { nb_mul_add(&p5_cache, 5, 0); p5_k = p5_k + 1; }
    *out = p5_cache;
}

/* Shared rounding core: value = q * 2^(e2 - blq) * (1 + eps), eps < 2^-blq,
 * `sticky` = the eps part is nonzero. q nonzero, blq = bitlen(q). */
static double sh_round_64(u64 q, int sticky, int blq, int e2, int neg) {
    int mant_bits, sh, rb, bump;
    u64 bits;
    if (q == 0) { return neg ? -0.0 : 0.0; }
    if (e2 - 1 > 1023) { return neg ? -__builtin_inf() : __builtin_inf(); }
    if (e2 <= -1074) {
        /* below the minimum denormal's binade: nearest of {0, 2^-1074}
         * with round-to-even on the exact half (tie -> 0) */
        u64 bits = (e2 == -1074 && blq < 64
                    && q > (((u64)1 << (blq - 1)))) ? 1 : 0;
        if (neg) { bits |= (u64)1 << 63; }
        return __builtin_bit_cast(double, bits);
    }
    if (e2 - 1 >= -1022) {
        mant_bits = 53;
    } else {
        mant_bits = 53 - (-1022 - (e2 - 1));
        if (mant_bits < 1) { mant_bits = 1; }
    }
    sh = blq - (mant_bits + 1);
    bump = 0;
    if (sh > 0) {
        if ((q & (((u64)1 << sh) - 1)) != 0) { sticky = 1; }
        q = q >> sh;
    } else if (sh < 0) {
        q = q << (-sh);
    }
    /* kept = mant_bits+1 bits: candidate(53) + round bit = kept LSB */
    rb = (int)(q & 1u);
    q = q >> 1;
    bump = rb != 0 && (sticky != 0 || (q & 1u) != 0);
    if (bump) { q = q + 1; }
    /* Bump can reach q = 2^mant_bits exactly. On the normal path
     * (mant_bits = 53) that is a real binade carry; on the denormal
     * path q = 2^mant_bits is already the correct encoding (raw bits,
     * and mant_bits = 52 with e2 = -1021 routes through the normal
     * assembly to exactly 2^-1022). Shifting there would collapse the
     * grid (value ~1.96*min rounding to min instead of 2*min). */
    if (mant_bits == 53 && (q >> mant_bits) != 0) {
        q = q >> 1;
        e2 = e2 + 1;
        if (e2 - 1 > 1023) { return neg ? -__builtin_inf() : __builtin_inf(); }
    }
    if (e2 - 1 >= -1022) {
        u64 frac = q & (((u64)1 << 52) - 1);
        bits = ((u64)(e2 - 1 + 1023) << 52) | frac;
    } else if ((q >> 53) != 0) {
        bits = (u64)1 << 52; /* rounding promoted to the smallest normal */
    } else {
        bits = q;
    }
    if (neg) { bits |= (u64)1 << 63; }
    return __builtin_bit_cast(double, bits);
}

/* Multiply path: value = B * 2^s exactly, B a bignum. */
static double sh_round(const nbig *m0, int s, int neg) {
    nbig B = *m0;
    int sticky = 0;
    int bitlen = nb_bitlen(&B);
    int e2, blq;
    u64 q;
    if (bitlen == 0) { return neg ? -0.0 : 0.0; }
    e2 = bitlen + s;
    if (e2 - 1 > 2000 || e2 < -2000 - 60) {
        /* far out of range: clamp early so shifts stay bounded */
        if (e2 - 1 > 1023) { return neg ? -__builtin_inf() : __builtin_inf(); }
        return neg ? -0.0 : 0.0;
    }
    if (bitlen > 56) {
        sticky = nb_shr_sticky(&B, bitlen - 56);
        blq = 56;
    } else {
        blq = bitlen;
    }
    q = (u64)B.l[0];
    if (B.n > 1) { q |= (u64)B.l[1] << 32; }
    q = q << (56 - blq);
    return sh_round_64(q, sticky, 56, e2, neg);
}

static int nb_cmp(const nbig *a, const nbig *b);
static void nb_sub(nbig *a, const nbig *b);

/* Division path: value = N / D * 2^s (D > 0). Restoring long division
 * from N's MSB (then fractional zero bits) for a 56-bit quotient prefix;
 * sticky = unconsumed remainder or any unconsumed low bit of N. */
static double sh_round_div(const nbig *N0, const nbig *D0, int s, int neg) {
    nbig rem;
    int blN = nb_bitlen(N0), blD = nb_bitlen(D0);
    int i, t, blq, e2, sticky = 0, got = 0;
    u64 q = 0;
    if (blN == 0) { return neg ? -0.0 : 0.0; }
    nb_init(&rem);
    for (i = blN - 1; got < 56 && i >= -(blD + 64); i = i - 1) {
        u32 bit = (i >= 0) ? ((N0->l[i >> 5] >> (i & 31)) & 1u) : 0u;
        u32 newtop = (rem.l[rem.n - 1] >> 31) & 1u;
        int j;
        for (j = rem.n - 1; j >= 1; j = j - 1) {
            rem.l[j] = (rem.l[j] << 1) | (rem.l[j - 1] >> 31);
        }
        rem.l[0] = (rem.l[0] << 1) | bit;
        if (newtop != 0 && rem.n < NBIG) {
            rem.l[rem.n] = 1u;
            rem.n = rem.n + 1;
        }
        if (nb_cmp(&rem, D0) >= 0) {
            nb_sub(&rem, D0);
            q = (q << 1) | 1u;
        } else {
            q = q << 1;
        }
        t = blN - 1 - i + 1; /* bits consumed so far */
        if (q != 0) { got = 64 - __builtin_clzll(q); }
    }
    t = blN - 1 - i; /* consumed through iteration i (post-decrement) */
    if (q == 0) { return neg ? -0.0 : 0.0; }
    if (nb_is_zero(&rem) == 0) { sticky = 1; }
    if (t < blN) {
        /* bits [0, blN - t) of N were never fed */
        int lowbits = blN - t;
        int li;
        for (li = 0; li < N0->n && li * 32 < lowbits; li = li + 1) {
            int lo = li * 32;
            u32 mask = (lo + 32) <= lowbits ? 0xffffffffu
                                            : (((u32)1 << (lowbits - lo)) - 1u);
            if ((N0->l[li] & mask) != 0) { sticky = 1; }
        }
    }
    blq = 64 - __builtin_clzll(q);
    /* value = (q + rem/D) * 2^(blN - t + s)  [q = floor(N*2^(t-blN)/D)] */
    e2 = blq + blN - t + s;
    return sh_round_64(q, sticky, blq, e2, neg);
}

static int nb_cmp(const nbig *a, const nbig *b) {
    int i;
    int an = a->n, bn = b->n;
    while (an > 1 && a->l[an - 1] == 0) { an = an - 1; }
    while (bn > 1 && b->l[bn - 1] == 0) { bn = bn - 1; }
    if (an != bn) { return an < bn ? -1 : 1; }
    for (i = an - 1; i >= 0; i = i - 1) {
        if (a->l[i] != b->l[i]) { return a->l[i] < b->l[i] ? -1 : 1; }
    }
    return 0;
}

static void nb_sub(nbig *a, const nbig *b) {
    int i;
    i64 borrow = 0;
    for (i = 0; i < a->n; i = i + 1) {
        i64 v = (i64)a->l[i] - borrow - (i < b->n ? (i64)b->l[i] : 0);
        if (v < 0) { v += 4294967296LL; borrow = 1; }
        else { borrow = 0; }
        a->l[i] = (u32)v;
    }
    nb_trim(a);
}

static double sh_hexfloat(const char *s, const char **end, int neg) {
    const char *p = s + 2; /* past 0x */
    u64 mant = 0;
    int sticky = 0, seen = 0, expo = 0, esign = 1;
    int e2, drop, rb, tl;
    u64 q;
    while (*p == '0') { seen = 1; p = p + 1; } /* leading zeros are free */
    for (; *p != 0; p = p + 1) {
        int d;
        if (*p >= '0' && *p <= '9') { d = *p - '0'; }
        else if (*p >= 'a' && *p <= 'f') { d = *p - 'a' + 10; }
        else if (*p >= 'A' && *p <= 'F') { d = *p - 'A' + 10; }
        else { break; }
        seen = 1;
        if (mant < ((u64)1 << 60)) {
            mant = (mant << 4) | (u64)d;
        } else {
            sticky = sticky | (d != 0);
            expo = expo + 4;
        }
    }
    if (*p == '.') {
        p = p + 1;
        for (; *p != 0; p = p + 1) {
            int d;
            if (*p >= '0' && *p <= '9') { d = *p - '0'; }
            else if (*p >= 'a' && *p <= 'f') { d = *p - 'a' + 10; }
            else if (*p >= 'A' && *p <= 'F') { d = *p - 'A' + 10; }
            else { break; }
            seen = 1;
            if (mant < ((u64)1 << 60)) {
                mant = (mant << 4) | (u64)d;
                expo = expo - 4;
            } else {
                sticky = sticky | (d != 0);
            }
        }
    }
    if (*p == 'p' || *p == 'P') {
        const char *q2 = p + 1;
        long ev = 0;
        int eany = 0;
        if (*q2 == '+') { q2 = q2 + 1; }
        else if (*q2 == '-') { esign = -1; q2 = q2 + 1; }
        while (*q2 >= '0' && *q2 <= '9') {
            ev = ev * 10 + (*q2 - '0');
            if (ev > 100000) { ev = 100000; }
            eany = 1;
            q2 = q2 + 1;
        }
        if (eany) { p = q2; expo = expo + esign * (int)ev; }
    }
    if (end != 0) { *(const char **)end = seen ? p : s; }
    if (!seen) { return 0.0; }
    if (mant == 0) { return neg ? -0.0 : 0.0; }
    e2 = 0;
    while ((mant & ((u64)1 << 63)) == 0) {
        mant = mant << 1;
        e2 = e2 - 1;
    }
    drop = 11; /* 64 -> 53 bits */
    q = mant;
    rb = (int)((q >> (drop - 1)) & 1u);
    tl = ((q & (((u64)1 << (drop - 1)) - 1)) != 0) || sticky;
    q = q >> drop;
    if (rb != 0 && (tl || (q & 1u) != 0)) { q = q + 1; }
    if ((q >> 53) != 0) { q = q >> 1; expo = expo + 1; }
    /* value = q * 2^(e2_out - 53) with mant normalized MSB at bit 63:
     * e2_out = expo + shifts + 64 */
    return sh_round_64(q, 0, 53, expo + e2 + 64, neg);
}

double strtod_full(const char *s, const char **end) {
    const char *p = s;
    int neg = 0;
    nbig mant;
    int digits = 0, dexp = 0, any = 0, eseen = 0, eesign = 1;
    long eexp = 0;
    while (sh_isspace(*p)) { p = p + 1; }
    if (*p == '+') { p = p + 1; }
    else if (*p == '-') { neg = 1; p = p + 1; }
    if ((p[0] == 'n' || p[0] == 'N')
        && (p[1] == 'a' || p[1] == 'A') && (p[2] == 'n' || p[2] == 'N')) {
        p = p + 3;
        if (end != 0) { *(const char **)end = p; }
        return __builtin_nan("");
    }
    if ((p[0] == 'i' || p[0] == 'I')
        && (p[1] == 'n' || p[1] == 'N') && (p[2] == 'f' || p[2] == 'F')) {
        p = p + 3;
        if ((p[0] == 'i' || p[0] == 'I')
            && (p[1] == 'n' || p[1] == 'N')
            && (p[2] == 'i' || p[2] == 'I')
            && (p[3] == 't' || p[3] == 'T')
            && (p[4] == 'y' || p[4] == 'Y')) {
            p = p + 5;
        }
        if (end != 0) { *(const char **)end = p; }
        return neg ? -__builtin_inf() : __builtin_inf();
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        return sh_hexfloat(p, end, neg);
    }
    nb_init(&mant);
    for (; *p >= '0' && *p <= '9'; p = p + 1) {
        any = 1;
        if (digits < 1100) {
            nb_mul_add(&mant, 10, (u32)(*p - '0'));
            digits = digits + 1;
        }
    }
    if (*p == '.') {
        p = p + 1;
        for (; *p >= '0' && *p <= '9'; p = p + 1) {
            any = 1;
            if (digits < 1100) {
                nb_mul_add(&mant, 10, (u32)(*p - '0'));
                digits = digits + 1;
                dexp = dexp - 1;
            }
        }
    }
    if (!any) {
        if (end != 0) { *(const char **)end = s; }
        return 0.0;
    }
    if (*p == 'e' || *p == 'E') {
        const char *q2 = p + 1;
        long ev = 0;
        int eany = 0;
        if (*q2 == '+') { q2 = q2 + 1; }
        else if (*q2 == '-') { eesign = -1; q2 = q2 + 1; }
        while (*q2 >= '0' && *q2 <= '9') {
            ev = ev * 10 + (*q2 - '0');
            if (ev > 100000) { ev = 100000; }
            eany = 1;
            q2 = q2 + 1;
        }
        if (eany) { p = q2; eseen = 1; eexp = eesign * ev; }
    }
    if (end != 0) { *(const char **)end = p; }
    (void)eseen;
    dexp = dexp + (int)eexp;
    if (nb_is_zero(&mant)) { return neg ? -0.0 : 0.0; }
    if (dexp > 1500) { dexp = 1500; }
    if (dexp < -1500) { dexp = -1500; }
    if (dexp >= 0) {
        nbig f, r;
        pow5(dexp, &f);
        nb_mul_big(&r, &mant, &f);
        return sh_round(&r, dexp, neg);
    }
    {
        nbig f;
        pow5(-dexp, &f);
        return sh_round_div(&mant, &f, dexp, neg);
    }
}

/* ---- exact %e/%f/%g over the strtod bignum machinery ---- */

static void nb_set_u64(nbig *b, u64 v) {
    if ((v >> 32) != 0) {
        b->l[0] = (u32)v;
        b->l[1] = (u32)(v >> 32);
        b->n = 2;
    } else {
        b->l[0] = (u32)v;
        b->n = 1;
    }
}

static void nb_inc(nbig *a) {
    int i = 0;
    u32 v = a->l[0] + 1u;
    a->l[0] = v;
    while (v == 0) {
        i = i + 1;
        if (i >= NBIG) { break; }
        v = a->l[i] + 1u;
        a->l[i] = v;
    }
    if (i >= a->n && a->l[a->n] != 0) { a->n = a->n + 1; }
}

static void nb_shl(nbig *a, int sh) {
    int lw = sh >> 5, rb = sh & 31;
    int i, nn;
    if (sh <= 0 || nb_is_zero(a)) { return; }
    nn = a->n + lw + 1;
    if (nn > NBIG) { nn = NBIG; }
    for (i = nn - 1; i >= 0; i = i - 1) {
        int src = i - lw;
        u32 lo = (src >= 0 && src < a->n) ? a->l[src] : 0;
        u32 v;
        if (rb != 0) {
            u32 hi = (src >= 1 && src - 1 < a->n) ? a->l[src - 1] : 0;
            v = (lo << rb) | (hi >> (32 - rb));
        } else {
            v = lo;
        }
        a->l[i] = v;
    }
    a->n = nn;
    nb_trim(a);
}

/* num = quot*den + rem (restoring shift-subtract, MSB first). Safe when
 * quot aliases num: bit i of num is read at the same step bit i of quot
 * is written, and the |= touches only that bit. */
static void nb_divmod(const nbig *num, const nbig *den, nbig *quot,
                      nbig *rem) {
    int i, blN, blD;
    nbig r, n2 = *num; /* num may alias quot: work off the copy */
    blN = nb_bitlen(&n2);
    blD = nb_bitlen(den);
    nb_init(quot);
    /* nb_init clears limb 0 only -- stale limbs above the quotient would
     * otherwise leak into the result through the |= below */
    for (i = 0; i < ((blN + 31) >> 5); i = i + 1) { quot->l[i] = 0; }
    nb_init(&r);
    if (blN < blD) {
        for (i = 0; i < num->n; i = i + 1) { rem->l[i] = num->l[i]; }
        rem->n = num->n;
        return;
    }
    for (i = blN - 1; i >= 0; i = i - 1) {
        u32 bit = (n2.l[i >> 5] >> (i & 31)) & 1u;
        u32 newtop = (r.l[r.n - 1] >> 31) & 1u;
        int j;
        for (j = r.n - 1; j >= 1; j = j - 1) {
            r.l[j] = (r.l[j] << 1) | (r.l[j - 1] >> 31);
        }
        r.l[0] = (r.l[0] << 1) | bit;
        if (newtop != 0 && r.n < NBIG) { r.l[r.n++] = 1u; }
        if (nb_cmp(&r, den) >= 0) {
            nb_sub(&r, den);
            quot->l[i >> 5] |= 1u << (i & 31);
        }
    }
    quot->n = (blN + 31) >> 5;
    nb_trim(quot);
    *rem = r;
    nb_trim(rem);
}

static u32 nb_divmod_small(nbig *a, u32 d, u32 *rem) {
    u64 r = 0;
    int i;
    for (i = a->n - 1; i >= 0; i = i - 1) {
        u64 cur = (r << 32) | (u64)a->l[i];
        a->l[i] = (u32)(cur / d);
        r = cur % d;
    }
    nb_trim(a);
    *rem = (u32)r;
    return (u32)r;
}

static int d2_digits(const nbig *q, char *dig) {
    nbig t = *q;
    int n = 0, i;
    u32 r;
    if (nb_is_zero(&t)) { dig[0] = '0'; dig[1] = 0; return 1; }
    while (!nb_is_zero(&t)) {
        nb_divmod_small(&t, 10, &r);
        dig[n++] = (char)('0' + r);
    }
    for (i = 0; i < n / 2; i = i + 1) {
        char c = dig[i];
        dig[i] = dig[n - 1 - i];
        dig[n - 1 - i] = c;
    }
    dig[n] = 0;
    return n;
}

/* value = mant * 2^e2 (exact, mant != 0): value >= 10^X ? */
static int d2_ge_pow10(u64 mant, int e2, int X) {
    nbig L, R, t;
    int d;
    if (X > 0) {
        pow5(X, &R);
        nb_set_u64(&L, mant);
        d = e2 - X;
        if (d >= 0) {
            nb_shl(&L, d);
        } else {
            nb_shl(&R, -d);
        }
        return nb_cmp(&L, &R) >= 0;
    }
    d = e2 - X; /* = e2 + |X| */
    if (d >= 0) { return 1; } /* mant*2^d*5^|X| >= 1 */
    pow5(-X, &R);
    nb_set_u64(&L, mant);
    nb_mul_big(&t, &L, &R); /* nb_mul_big zeroes r: no input aliasing */
    L = t;
    nb_init(&t);
    nb_set_u64(&t, 1);
    nb_shl(&t, -d);
    return nb_cmp(&L, &t) >= 0;
}

/* q = round-half-even(mant * 2^e2 * 10^s), exact. The 5^|s| part goes
 * through the pow5 cache (multiply or divide-with-remainder), the
 * 2^part through shifts with the dropped bits kept for the half test:
 * round up iff 2*(low*5^a + rem) > 5^a<<b, tie -> even q. */
static void d2_round_pow10(nbig *q, u64 mant, int e2, int s) {
    nbig den, rem, low, lhs, rhs;
    int a = s >= 0 ? s : -s;
    int bexp = e2 + s;
    int b = bexp < 0 ? -bexp : 0;
    int have_rem = 0;
    nb_set_u64(q, mant);
    nb_init(&rem);
    nb_init(&den);
    /* Scale the 2-power part first: with bexp >= 0 the integer part may
     * only materialize through the shift, so the 5^a division must see
     * the shifted value (dividing first would floor it away). */
    if (bexp >= 0) {
        if (bexp > 0) { nb_shl(q, bexp); }
        if (s >= 0) {
            if (a > 0) {
                nbig t2;
                pow5(a, &den);
                nb_mul_big(&t2, q, &den); /* no input aliasing */
                *q = t2;
            }
        } else {
            pow5(a, &den);
            nb_divmod(q, &den, q, &rem);
            nb_shl(&rem, 1);
            {
                int c = nb_cmp(&rem, &den);
                if (c > 0 || (c == 0 && (q->l[0] & 1u) != 0)) { nb_inc(q); }
            }
        }
        return;
    }
    if (s >= 0) {
        if (a > 0) {
            nbig t2;
            pow5(a, &den);
            nb_mul_big(&t2, q, &den); /* no input aliasing */
            *q = t2;
        }
    } else if (a > 0) {
        pow5(a, &den);
        nb_divmod(q, &den, q, &rem);
        have_rem = nb_is_zero(&rem) ? 0 : 1;
    }
    nb_init(&low);
    {
        int nl = (b + 31) >> 5;
        int i;
        for (i = 0; i < q->n && i < nl; i = i + 1) { low.l[i] = q->l[i]; }
        low.n = q->n < nl ? q->n : nl;
        if (low.n == nl && (b & 31) != 0) {
            low.l[nl - 1] &= (((u32)1 << (b & 31)) - 1u);
        }
        nb_trim(&low);
    }
    nb_shr_sticky(q, b);
    if (a == 0 && b == 0) { return; }
    if (a > 0) {
        pow5(a, &den);
        nb_mul_big(&lhs, &low, &den);
        rhs = den;
    } else {
        lhs = low;
        nb_init(&rhs);
        nb_set_u64(&rhs, 1);
    }
    if (have_rem) {
        int n = rem.n > lhs.n ? rem.n : lhs.n;
        u64 carry = 0;
        int i;
        for (i = 0; i < n; i = i + 1) {
            u64 v = carry;
            if (i < lhs.n) { v += lhs.l[i]; }
            if (i < rem.n) { v += rem.l[i]; }
            lhs.l[i] = (u32)v;
            carry = v >> 32;
        }
        lhs.n = n;
        if (carry != 0 && lhs.n < NBIG) { lhs.l[lhs.n++] = (u32)carry; }
    }
    {
        u64 carry = 0;
        int i;
        for (i = 0; i < lhs.n; i = i + 1) {
            u64 v = ((u64)lhs.l[i] << 1) | carry;
            lhs.l[i] = (u32)v;
            carry = v >> 32;
        }
        if (carry != 0 && lhs.n < NBIG) { lhs.l[lhs.n++] = (u32)carry; }
    }
    nb_shl(&rhs, b);
    {
        int c = nb_cmp(&lhs, &rhs);
        if (c > 0) {
            nb_inc(q);
        } else if (c == 0 && (q->l[0] & 1u) != 0) {
            nb_inc(q);
        }
    }
}

/* C-printf-faithful renderer for one finite-or-special double.
 * style: 'e'/'E' (prec digits after point), 'f'/'F' (prec after point),
 * 'g'/'G' (prec significant digits, trailing zeros stripped, e vs f by
 * the rounded exponent: e iff X < -4 || X >= prec). */
static int zan_fmt_double(char *out, double v, int prec, char style) {
    u64 bits = __builtin_bit_cast(u64, v);
    int neg = (int)(bits >> 63);
    int be = (int)((bits >> 52) & 0x7ff);
    u64 fr = bits & (((u64)1 << 52) - 1);
    u64 mant;
    int e2, i;
    char *o = out;
    char dig[1504];
    if (prec < 0) { prec = 6; }
    if (prec > 1080) { prec = 1080; }
    if (be == 0x7ff) {
        if (neg) { *o++ = '-'; }
        if (fr != 0) { *o++ = 'n'; *o++ = 'a'; *o++ = 'n'; }
        else { *o++ = 'i'; *o++ = 'n'; *o++ = 'f'; }
        *o = 0;
        return (int)(o - out);
    }
    if (be == 0) {
        mant = fr;
        e2 = -1074;
    } else {
        mant = fr | ((u64)1 << 52);
        e2 = be - 1075;
    }
    if (neg) { *o++ = '-'; }
    if (mant == 0) {
        if (style == 'g' || style == 'G') {
            *o++ = '0';
        } else if (style == 'e' || style == 'E') {
            *o++ = '0';
            *o++ = '.';
            for (i = 0; i < prec; i = i + 1) { *o++ = '0'; }
            *o++ = (style == 'E') ? 'E' : 'e';
            *o++ = '+';
            *o++ = '0';
            *o++ = '0';
        } else {
            *o++ = '0';
            if (prec > 0) {
                *o++ = '.';
                for (i = 0; i < prec; i = i + 1) { *o++ = '0'; }
            }
        }
        *o = 0;
        return (int)(o - out);
    }
    if (style == 'g' || style == 'G') {
        if (prec == 0) { prec = 1; }
    }
    {
        nbig q;
        int X;
        int bl = e2 + (64 - __builtin_clzll(mant));
        X = (int)((double)bl * 0.30102999566398119521);
        while (X > -400 && !d2_ge_pow10(mant, e2, X)) { X = X - 1; }
        while (X < 400 && d2_ge_pow10(mant, e2, X + 1)) { X = X + 1; }
        if (style == 'f' || style == 'F') {
            int nd;
            d2_round_pow10(&q, mant, e2, prec);
            nd = d2_digits(&q, dig);
            if (nd <= prec) {
                *o++ = '0';
                if (prec > 0) {
                    *o++ = '.';
                    for (i = 0; i < prec - nd; i = i + 1) { *o++ = '0'; }
                    for (i = 0; i < nd; i = i + 1) { *o++ = dig[i]; }
                }
            } else {
                for (i = 0; i < nd - prec; i = i + 1) { *o++ = dig[i]; }
                if (prec > 0) {
                    *o++ = '.';
                    for (i = nd - prec; i < nd; i = i + 1) { *o++ = dig[i]; }
                }
            }
        } else {
            int sig = (style == 'e' || style == 'E') ? prec + 1 : prec;
            int nd, estrail;
            d2_round_pow10(&q, mant, e2, sig - 1 - X);
            nd = d2_digits(&q, dig);
            if (nd == sig + 1) { X = X + 1; } /* carried to 10^sig */
            if (style == 'g' || style == 'G') {
                estrail = (X < -4 || X >= sig);
                while (nd > 1 && dig[nd - 1] == '0') { nd = nd - 1; }
            } else {
                estrail = 1;
            }
            if (estrail) {
                int xe = X;
                int ecap = (style == 'e' || style == 'E') ? sig : nd;
                *o++ = dig[0];
                if (ecap > 1) {
                    *o++ = '.';
                    for (i = 1; i < nd && i < ecap; i = i + 1) {
                        *o++ = dig[i];
                    }
                }
                *o++ = (style == 'E' || style == 'G') ? 'E' : 'e';
                if (xe < 0) { *o++ = '-'; xe = -xe; } else { *o++ = '+'; }
                {
                    int e1 = xe / 100, e10 = (xe / 10) % 10, e3 = xe % 10;
                    if (e1 > 0) {
                        *o++ = (char)('0' + e1);
                    }
                    *o++ = (char)('0' + e10);
                    *o++ = (char)('0' + e3);
                }
            } else if (X >= 0) {
                for (i = 0; i <= X && i < nd; i = i + 1) { *o++ = dig[i]; }
                for (; i <= X; i = i + 1) { *o++ = '0'; }
                if (i < nd) {
                    *o++ = '.';
                    for (; i < nd; i = i + 1) { *o++ = dig[i]; }
                }
            } else {
                *o++ = '0';
                *o++ = '.';
                for (i = 0; i < -X - 1; i = i + 1) { *o++ = '0'; }
                for (i = 0; i < nd; i = i + 1) { *o++ = dig[i]; }
            }
        }
    }
    *o = 0;
    return (int)(o - out);
}

/* libc-name wrapper: the emitted runtime's double.Parse lowers to strtod. */
double strtod(const char *s, char **end) {
    return strtod_full(s, (const char **)end);
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

/* Per-thread EH state storage for the compiler-emitted _zan_rt_eh_state.
 * The guest is single-threaded bare metal, so one static block is the
 * honest per-"thread" state; the block layout is compiler-owned
 * ({i32 top, ptr exc, 256 x 1024-byte setjmp slots}). */
static char zan_eh_block[16 + 256 * 1024] __attribute__((aligned(16)));
void *zan_eh_tls_state(void) { return zan_eh_block; }

/* setjmp/longjmp: the emitted runtime's EH lowers to them, so bare metal
 * needs the pair. Both sides are ours, so the jmp_buf layout is
 * self-consistent (glibc-compat aarch64 shape: x19-x28, fp/lr, sp,
 * d8-d15 -- 168 bytes, fits the runtime's 1024-byte setjmp slots).
 * Top-level asm: a C body would let the compiler touch x30/lr in its
 * own prologue before the capture. */
__asm__(
".text\n"
".align 4\n"
".globl setjmp\n"
".type setjmp, %function\n"
"setjmp:\n"
"    stp x19, x20, [x0]\n"
"    stp x21, x22, [x0, #16]\n"
"    stp x23, x24, [x0, #32]\n"
"    stp x25, x26, [x0, #48]\n"
"    stp x27, x28, [x0, #64]\n"
"    stp x29, x30, [x0, #80]\n"
"    mov x1, sp\n"
"    str x1, [x0, #96]\n"
"    stp d8, d9, [x0, #104]\n"
"    stp d10, d11, [x0, #120]\n"
"    stp d12, d13, [x0, #136]\n"
"    stp d14, d15, [x0, #152]\n"
"    mov w0, wzr\n"
"    ret\n"
".globl longjmp\n"
".type longjmp, %function\n"
"longjmp:\n"
"    ldp x19, x20, [x0]\n"
"    ldp x21, x22, [x0, #16]\n"
"    ldp x23, x24, [x0, #32]\n"
"    ldp x25, x26, [x0, #48]\n"
"    ldp x27, x28, [x0, #64]\n"
"    ldp x29, x30, [x0, #80]\n"
"    ldr x2, [x0, #96]\n"
"    ldp d8, d9, [x0, #104]\n"
"    ldp d10, d11, [x0, #120]\n"
"    ldp d12, d13, [x0, #136]\n"
"    ldp d14, d15, [x0, #152]\n"
"    cmp w1, #0\n"
"    cinc w0, w1, eq\n"
"    mov sp, x2\n"
"    br x30\n");

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
        /* qemu's reset SCTLR_EL1 leaves SA on; the stub is compiled
         * -mstrict-align and the emitted runtime keeps scalar accesses
         * aligned, so clear the alignment checks to match the hosted
         * lanes (macOS, Linux user mode) in case anything slips. */
        "mrs x0, sctlr_el1\n"
        "bic x0, x0, #(1 << 1)\n"
        "bic x0, x0, #(1 << 3)\n"
        "msr sctlr_el1, x0\n"
        "isb\n"
        "adrp x0, stack_area\n"
        "add x0, x0, :lo12:stack_area\n"
        "mov x9, #0x100000\n"          /* 1 MiB stack */
        "add sp, x0, x9\n"
        "bl boot\n"
        "b .\n");
}
