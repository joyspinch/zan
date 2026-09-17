/* Transitional Zan C-host runtime stubs (RT_OBJS): the full-stdlib pull
 * references the gen0 host entry points (NativeMemory.*, audio, Win codepage
 * APIs) that ngen_host maps to BLExtern. Implemented over libc where the
 * semantics are unambiguous; abort() where a call would mean an unported
 * path. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

void *Alloc(int size) { return calloc(1, (size_t)(size > 0 ? size : 1)); }
void Free(void *p) { free(p); }
void Copy(void *dst, void *src, int n) { memmove(dst, src, (size_t)n); }
void Fill(void *p, int v, int n) { memset(p, v, (size_t)n); }
int Compare(void *a, void *b, int n) { return memcmp(a, b, (size_t)n); }
int Find(void *p, int off, int b, int n) {
    unsigned char *hit = memchr((unsigned char *)p + off, b, (size_t)n);
    return hit ? (int)(hit - (unsigned char *)p) : -1;
}
void PutString(void *p, int off, unsigned char *s, int len) {
    if (s && len > 0) memcpy((unsigned char *)p + off, s, (size_t)len);
}
/* string GetString(nint p, int off, int len): the oracle inlines this
 * (irgen_expr.c) into alloc+memcpy+NUL+len-stamp; we emit BLExtern, so
 * build the same object by hand: raw = [rc:8][header:8][data][NUL],
 * header = 0x5A414E53_00000000 | len, user pointer = raw + 16. */
void *GetString(void *p, int off, int len) {
    if (len < 0 || p == 0) { len = 0; }
    char *raw = (char *)malloc((size_t)len + 17);
    if (!raw) { return 0; }
    *(long long *)(void *)raw = 1;                       /* rc = 1 */
    *(long long *)(void *)(raw + 8) = (long long)0x5A414E5300000000LL | (long long)(unsigned int)len;
    unsigned char *data = (unsigned char *)raw + 16;
    if (len > 0) { memcpy(data, (unsigned char *)p + off, (size_t)len); }
    data[len] = 0;
    return data;
}

/* int Crc32(nint p, int len): IEEE 802.3 reflected CRC32 (poly 0xEDB88320),
 * table-driven, matching the oracle's self-contained nm_crc32_fn. */
long long Crc32(void *p, long long len) {
    static unsigned int table[256];
    static int ready = 0;
    if (!ready) {
        for (unsigned int i = 0; i < 256; i++) {
            unsigned int c = i;
            for (int k = 0; k < 8; k++) { c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); }
            table[i] = c;
        }
        ready = 1;
    }
    unsigned int c = 0xFFFFFFFFu;
    unsigned char *b = (unsigned char *)p;
    for (long long i = 0; p && i < len; i++) { c = table[(c ^ b[i]) & 0xFF] ^ (c >> 8); }
    /* zero-extend: the oracle widens zext i32->i64 (irgen.zan), so a signed
     * int return prints the crc negative from Zan */
    return (long long)(c ^ 0xFFFFFFFFu);
}
/* void zan_sha512(data, len, md): FIPS 180-4 SHA-512, self-contained — the
 * emitted runtime calls this instead of CC_SHA512, which misbehaved through
 * the chained-fixup stub path (a direct C call in the same binary produced
 * the correct digest while the stub call did not; the oracle also inlines
 * its digest implementations, so self-containment is the house style). */
static unsigned long long zsha_rot64(unsigned long long x, int n) {
    return (x >> n) | (x << (64 - n));
}
static void zsha_block(unsigned long long *h, const unsigned char *p) {
    static const unsigned long long K[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
        0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
    };
    unsigned long long w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = 0;
        for (int j = 0; j < 8; j++) { w[i] = (w[i] << 8) | p[i * 8 + j]; }
    }
    for (int i = 16; i < 80; i++) {
        unsigned long long s0 = zsha_rot64(w[i-15], 1) ^ zsha_rot64(w[i-15], 8) ^ (w[i-15] >> 7);
        unsigned long long s1 = zsha_rot64(w[i-2], 19) ^ zsha_rot64(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    unsigned long long a = h[0], b = h[1], c = h[2], d = h[3];
    unsigned long long e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 80; i++) {
        unsigned long long S1 = zsha_rot64(e, 14) ^ zsha_rot64(e, 18) ^ zsha_rot64(e, 41);
        unsigned long long ch = (e & f) ^ (~e & g);
        unsigned long long t1 = hh + S1 + ch + K[i] + w[i];
        unsigned long long S0 = zsha_rot64(a, 28) ^ zsha_rot64(a, 34) ^ zsha_rot64(a, 39);
        unsigned long long maj = (a & b) ^ (a & c) ^ (b & c);
        unsigned long long t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
void zan_sha512(const void *data, long long len, unsigned char *md) {
    unsigned long long h[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
    };
    const unsigned char *msg = (const unsigned char *)data;
    long long total = len > 0 ? len : 0;
    long long off = 0;
    while (off + 128 <= total) { zsha_block(h, msg + off); off += 128; }
    unsigned char block[128];
    long long tail = total - off;                 /* 0..127 */
    memcpy(block, msg + off, (size_t)tail);
    block[tail] = 0x80;
    if (tail + 1 > 112) {
        /* no room for the 128-bit length in this block: flush it and use a
         * fresh all-zero final block */
        memset(block + tail + 1, 0, (size_t)(128 - tail - 1));
        zsha_block(h, block);
        memset(block, 0, 120);
        tail = -1;
    }
    memset(block + tail + 1, 0, (size_t)(120 - tail - 1));
    unsigned long long bits = (unsigned long long)total * 8;
    for (int i = 0; i < 8; i++) { block[120 + i] = (unsigned char)(bits >> (56 - 8 * i)); }
    zsha_block(h, block);
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) { md[i * 8 + j] = (unsigned char)(h[i] >> (56 - 8 * j)); }
    }
}

long long zan_monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000ll + (long long)ts.tv_nsec;
}
int MultiByteToWideChar(unsigned int cp, unsigned long fl, const char *s, int sl, unsigned short *w, int wl) { (void)cp;(void)fl;(void)s;(void)sl;(void)w;(void)wl; return 0; }
int WideCharToMultiByte(unsigned int cp, unsigned long fl, const unsigned short *w, int wl, char *s, int sl, char *dc, int *du) { (void)cp;(void)fl;(void)w;(void)wl;(void)s;(void)sl;(void)dc;(void)du; return 0; }

int zan_audio_open(void) { return 0; }
void zan_audio_close(void) { }
int zan_audio_is_open(void) { return 0; }
const char *zan_audio_driver_name(void) { return "stub"; }
const char *zan_audio_last_error(void) { return "stub"; }
double zan_audio_volume(void) { return 0.0; }
void zan_audio_set_volume(double v) { (void)v; }
int zan_audio_active_voices(void) { return 0; }
void zan_audio_stop_all(void) { }
long long zan_audio_load_wav(const void *d, long long n) { (void)d;(void)n; return 0; }
long long zan_audio_load_ogg(const void *d, long long n) { (void)d;(void)n; return 0; }
long long zan_audio_load_wav_mem(const void *d, long long n) { (void)d;(void)n; return 0; }
long long zan_audio_load_ogg_mem(const void *d, long long n) { (void)d;(void)n; return 0; }
void zan_audio_free_clip(long long c) { (void)c; }
double zan_audio_clip_duration_ms(long long c) { (void)c; return 0.0; }
int zan_audio_clip_channels(long long c) { (void)c; return 0; }
int zan_audio_clip_frequency(long long c) { (void)c; return 0; }
int zan_audio_play(long long c, int loop) { (void)c;(void)loop; return -1; }
int zan_audio_voice_playing(int v) { (void)v; return 0; }
void zan_audio_voice_stop(int v) { (void)v; }
void zan_audio_voice_set_gain(int v, double g) { (void)v;(void)g; }
