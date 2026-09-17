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

/* ---- Directory.zan: fully-resolved absolute path (NULL on failure) ---- */
char *zan_file_read_path(const char *path) {
    char buf[4096];
    if (!realpath(path, buf)) { return NULL; }
    return strdup(buf);
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
    return fseek(h2f(h), (long)off, origin);
}
long zan_file_tell(long h) { return h ? (long)ftell(h2f(h)) : -1; }
long zan_file_flush(long h) { return h ? (long)fflush(h2f(h)) : -1; }
long zan_file_close(long h) { if (!h) { return 0; } return fclose(h2f(h)); }
long zan_file_eof(long h) { return h ? (feof(h2f(h)) ? 1 : 0) : 1; }

/* ---- FileInfo.zan / FileInfoEx.zan ----
 * attributes: bit0 readonly, bit1 hidden (leading '.'), bit2 directory;
 * -1 = path missing. time: 0 = mtime, 1 = creation (birthtime), 2 = atime. */
long zan_file_length(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || (st.st_mode & S_IFMT) == S_IFDIR) { return -1; }
    return (long)st.st_size;
}
long zan_file_attributes(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) { return -1; }
    long a = 0;
    if ((st.st_mode & S_IFMT) != S_IFDIR && (st.st_mode & 0222) == 0) { a |= 1; }
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (base[0] == '.') { a |= 2; }
    if ((st.st_mode & S_IFMT) == S_IFDIR) { a |= 4; }
    return a;
}
long zan_file_time(const char *path, int which) {
    struct stat st;
    if (stat(path, &st) != 0) { return -1; }
    struct timespec ts;
    if (which == 0) { ts = st.st_mtimespec; }
    else if (which == 1) { ts = st.st_birthtimespec; }
    else { ts = st.st_atimespec; }
    return (long)ts.tv_sec;
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

/* ---- File.zan TryLock/Unlock: the lock IS the open handle; the kernel
 * releases it when the process dies, so no stale pid-file markers. ---- */
long zan_file_try_lock(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) { return 0; }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); return 0; }
    return (long)fd;
}
long zan_file_unlock(long h) {
    if (!h) { return 0; }
    flock((int)h, LOCK_UN);
    close((int)h);
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
