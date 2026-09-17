/* Transitional host externs (RT_OBJS second object): the zan_file_* /
 * zan_embed_* / zan_mmap_* / zan_pkg_fopen family declared by the pulled
 * stdlib, implemented over libc. The compiler binary itself only exercises
 * zan_pkg_fopen + libc (ReadAllText/WriteAllText/ListNames); the rest of the
 * family exists so pulled stdlib method bodies link. Programs compiled BY
 * this compiler additionally reference the zan_ext / zan_dt / zan_dir helper
 * families (see ngen_host.zan) — those live in the full runtime objects, not
 * here. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <time.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static FILE *h2f(long h) { return (FILE *)(size_t)h; }

/* ---- package + embed: zanc embeds nothing, local files always win ---- */
FILE *zan_pkg_fopen(const char *path, const char *mode) { return fopen(path, mode); }
int zan_embed_has(const char *name) { (void)name; return 0; }
const char *zan_embed_read(const char *name) { (void)name; return NULL; }
void *zan_embed_bytes(const char *name, int *outLen) { (void)name; if (outLen) *outLen = 0; return NULL; }
const char *zan_embed_list(const char *prefix) { (void)prefix; return ""; }

/* ---- File.zan fopen/remove/rename (DllImport EntryPoint remaps) ---- */
FILE *zan_file_fopen(const char *path, const char *mode) { return fopen(path, mode); }
int zan_file_remove(const char *path) { return remove(path); }
int zan_file_rename(const char *oldp, const char *newp) { return rename(oldp, newp); }

/* ---- Directory.zan read-path resolution, matching rt_file.c semantics:
 * NOT realpath. A relative path that exists in the working directory
 * resolves to "" (the caller then keeps its own spelling); a missing
 * relative path falls back to the packaged copy under $ZAN_PKG_DIR or next
 * to the executable; absolute paths and misses return "". Returning the
 * argument itself is never allowed (the managed side would release an
 * unretained managed string). */
static long zanh_exists(const char *p) {
    struct stat st;
    return (p && p[0] && stat(p, &st) == 0) ? 0 : -1;
}

static const char *zanh_app_dir(void) {
    static char dir[4096];
    const char *env = getenv("ZAN_APP_DIR");
    if (env && env[0] && strlen(env) < sizeof(dir)) { return env; }
    char exe[4096];
    uint32_t cap = (uint32_t)sizeof(exe);
    dir[0] = '\0';
    if (_NSGetExecutablePath(exe, &cap) != 0) { return dir; }
    char *sep = strrchr(exe, '/');
    if (sep && sep != exe) {
        *sep = '\0';
        if (strlen(exe) < sizeof(dir)) { memcpy(dir, exe, strlen(exe) + 1); }
    }
    return dir;
}

char *zan_file_read_path(const char *path) {
    if (zanh_exists(path) >= 0) { return strdup(""); }
    if (path[0] != '/' && path[0] != '\\' && path[1] != ':') {
        for (int which = 0; which < 2; which++) {
            const char *base = which == 0 ? getenv("ZAN_PKG_DIR") : zanh_app_dir();
            if (!base || !base[0]) { continue; }
            char alt[4096];
            if (strlen(base) + strlen(path) + 2 > sizeof(alt)) { continue; }
            sprintf(alt, "%s/%s", base, path);
            if (zanh_exists(alt) >= 0) { return strdup(alt); }
        }
    }
    return strdup("");
}

/* ---- FileStream.zan: FILE*-backed handle, 0 = closed/failed ---- */
long zan_file_open(const char *path, const char *mode) {
    FILE *f = fopen(path, mode);
    return f ? (long)(size_t)f : 0;
}
long zan_file_read(long h, void *buf, long count) {
    if (!h) { return -1; }
    return (long)fread(buf, 1, (size_t)count, h2f(h));
}
long zan_file_write(long h, void *buf, long count) {
    if (!h) { return -1; }
    return (long)fwrite(buf, 1, (size_t)count, h2f(h));
}
long zan_file_seek(long h, long off, int origin) {
    if (!h) { return -1; }
    /* returns the resulting absolute offset (like the oracle's
     * fseeko!=0 ? -1 : ftello), not fseek's success flag -- FileStream.Length
     * reads the size as seek(handle, 0, SEEK_END) */
    if (fseek(h2f(h), (long)off, origin) != 0) { return -1; }
    return (long)ftell(h2f(h));
}
long zan_file_tell(long h) { return h ? (long)ftell(h2f(h)) : -1; }
long zan_file_flush(long h) { return h ? (long)fflush(h2f(h)) : -1; }
long zan_file_close(long h) { if (!h) { return 0; } return fclose(h2f(h)); }
long zan_file_eof(long h) { return h ? (feof(h2f(h)) ? 1 : 0) : 1; }

/* ---- FileInfo.zan / FileInfoEx.zan ----
 * attributes: bit0 readonly, bit1 hidden (leading '.'), bit2 directory;
 * -1 = path missing. time: 0 = mtime, 1 = creation (st_ctime on POSIX,
 * like the oracle), 2 = atime. length: -1 only when stat fails (the oracle
 * reports directory sizes too). */
long zan_file_length(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) { return -1; }
    return (long)st.st_size;
}
long zan_file_attributes(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) { return -1; }
    long a = 0;
    if (access(path, W_OK) != 0) { a |= 1; }
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (base[0] == '.') { a |= 2; }
    if ((st.st_mode & S_IFMT) == S_IFDIR) { a |= 4; }
    return a;
}
long zan_file_time(const char *path, int which) {
    /* oracle rt_file.c: missing/empty path -> 0 (not -1) */
    if (!path || !path[0]) { return 0; }
    struct stat st;
    if (stat(path, &st) != 0) { return 0; }
    if (which == 1) { return (long)st.st_ctime; }
    if (which == 2) { return (long)st.st_atime; }
    return (long)st.st_mtime;
}
long zan_file_set_readonly(const char *path, int on) {
    struct stat st;
    if (stat(path, &st) != 0) { return 0; }
    mode_t m = st.st_mode;
    if (on) { m &= ~(mode_t)0222; } else { m |= 0222; }
    return chmod(path, m) == 0 ? 1 : 0;
}
long zan_file_set_time(const char *path, int which, long unixSec) {
    struct stat st;
    if (stat(path, &st) != 0) { return 0; }
    struct timespec ts[2];
    ts[0] = st.st_atimespec;
    ts[1] = st.st_mtimespec;
    struct timespec want;
    want.tv_sec = (time_t)unixSec;
    want.tv_nsec = 0;
    if (which == 0) { ts[1] = want; }
    else if (which == 1) { return 0; }   /* birthtime is not settable */
    else { ts[0] = want; }
    return utimensat(AT_FDCWD, path, ts, 0) == 0 ? 1 : 0;
}

/* ---- File.zan TryLock/Unlock: handles encode (gen << 32) | slot into a
 * small table, like the oracle (rt_file.c): the generation makes a second
 * unlock of the same handle value fail (returns 0) instead of releasing
 * whatever lock now occupies the slot. ---- */
#define ZAN_LK_CAP 32
static struct { int fd; int used; unsigned int gen; } g_lk_table[ZAN_LK_CAP];

static long zan_lk_index(long long h) {
    if (h <= 0) { return -1; }
    unsigned long long u = (unsigned long long)h;
    unsigned int gen = (unsigned int)(u >> 32);
    unsigned long long slot = u & 0xFFFFFFFFULL;
    if (slot >= ZAN_LK_CAP) { return -1; }
    if (!g_lk_table[slot].used || g_lk_table[slot].gen != gen) { return -1; }
    return (long)slot;
}

long zan_file_try_lock(const char *path) {
    if (!path || !path[0]) { return 0; }
    long slot = -1;
    for (long i = 0; i < ZAN_LK_CAP; i++) {
        if (!g_lk_table[i].used) { slot = i; g_lk_table[i].used = 1; break; }
    }
    if (slot < 0) { return 0; }
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) { g_lk_table[slot].used = 0; return 0; }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); g_lk_table[slot].used = 0; return 0; }
    unsigned int next = g_lk_table[slot].gen + 1;
    if (next == 0) {  /* wrap: retire the slot like the oracle */
        close(fd);
        g_lk_table[slot].used = 2;
        return 0;
    }
    g_lk_table[slot].gen = next;
    g_lk_table[slot].fd = fd;
    return (long)(((unsigned long long)next << 32) | (unsigned long long)slot);
}

long zan_file_unlock(long long h) {
    long slot = zan_lk_index(h);
    if (slot < 0) { return 0; }
    int fd = g_lk_table[slot].fd;
    g_lk_table[slot].fd = -1;
    g_lk_table[slot].used = 0;
    unsigned int ng = g_lk_table[slot].gen + 1;
    if (ng == 0) { ng = 1; }
    g_lk_table[slot].gen = ng;   /* a second unlock of the same value fails */
    close(fd);                   /* drops the flock */
    return 1;
}

/* ---- MemoryMappedFile.zan: nothing in the bootstrap exercises mmap ---- */
long zan_mmap_create(const char *n, long s) { (void)n; (void)s; return 0; }
long zan_mmap_open(const char *n, long s) { (void)n; (void)s; return 0; }
long zan_mmap_from_file(const char *p, long s) { (void)p; (void)s; return 0; }
long zan_mmap_map(long h, long s) { (void)h; (void)s; return 0; }
long zan_mmap_unmap(long p, long s) { (void)p; (void)s; return 0; }
long zan_mmap_flush(long p, long s) { (void)p; (void)s; return 0; }
long zan_mmap_close(long h) { (void)h; return 0; }
long zan_mmap_unlink(const char *n) { (void)n; return 0; }
