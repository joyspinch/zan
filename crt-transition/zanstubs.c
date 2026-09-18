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

/* void zan_sha256(data, len, md): FIPS 180-4 SHA-256, self-contained like
 * zan_sha512 — the NativeMemory.Sha256 intrinsic's ngen lowering calls this
 * with (data, len>=0 clamped by the caller, out32). Same house style as the
 * oracle, which inlines its own digest code. */
static unsigned int zsha_rot32(unsigned int x, int n) {
    return (x >> n) | (x << (32 - n));
}
static void zsha256_block(unsigned int *h, const unsigned char *p) {
    static const unsigned int K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    unsigned int w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16)
             | ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        unsigned int s0 = zsha_rot32(w[i-15], 7) ^ zsha_rot32(w[i-15], 18) ^ (w[i-15] >> 3);
        unsigned int s1 = zsha_rot32(w[i-2], 17) ^ zsha_rot32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    unsigned int a = h[0], b = h[1], c = h[2], d = h[3];
    unsigned int e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        unsigned int S1 = zsha_rot32(e, 6) ^ zsha_rot32(e, 11) ^ zsha_rot32(e, 25);
        unsigned int ch = (e & f) ^ (~e & g);
        unsigned int t1 = hh + S1 + ch + K[i] + w[i];
        unsigned int S0 = zsha_rot32(a, 2) ^ zsha_rot32(a, 13) ^ zsha_rot32(a, 22);
        unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
        unsigned int t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
void zan_sha256(const void *data, long long len, unsigned char *md) {
    unsigned int h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    const unsigned char *msg = (const unsigned char *)data;
    long long total = len > 0 ? len : 0;
    long long off = 0;
    while (off + 64 <= total) { zsha256_block(h, msg + off); off += 64; }
    unsigned char block[64];
    long long tail = total - off;                 /* 0..63 */
    memcpy(block, msg + off, (size_t)tail);
    block[tail] = 0x80;
    if (tail + 1 > 56) {
        memset(block + tail + 1, 0, (size_t)(64 - tail - 1));
        zsha256_block(h, block);
        memset(block, 0, 56);
        tail = -1;
    }
    memset(block + tail + 1, 0, (size_t)(56 - tail - 1));
    unsigned long long bits = (unsigned long long)total * 8;
    for (int i = 0; i < 8; i++) { block[56 + i] = (unsigned char)(bits >> (56 - 8 * i)); }
    zsha256_block(h, block);
    for (int i = 0; i < 8; i++) {
        md[i * 4] = (unsigned char)(h[i] >> 24);
        md[i * 4 + 1] = (unsigned char)(h[i] >> 16);
        md[i * 4 + 2] = (unsigned char)(h[i] >> 8);
        md[i * 4 + 3] = (unsigned char)(h[i]);
    }
}

/* ---- AES-GCM EVP shim (NIST SP 800-38D) ----
 * The stdlib's AesGcm.zan drives OpenSSL's EVP interface; the oracle links
 * @rpath/libcrypto.3 for it. We implement the exact same semantics
 * self-contained: two-phase Init_ex (cipher, then SET_IVLEN + key/iv),
 * AAD via Update(out=NULL), CTR message path, GET_TAG after Final /
 * SET_TAG before decrypt Final, and Final_ex returning 0 on tag mismatch.
 * Strings/byte[] arrive as raw byte pointers (payload / element 0). */

static const unsigned char ZAES_SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static void zaes_expand_key(const unsigned char *key, int keybits,
                            unsigned char *rk, int *nrounds) {
    int nk = keybits / 32;                    /* 4 / 6 / 8 words */
    int words = 4 * (nk + 6 + 1);             /* 4*(Nr+1), Nr = nk+6 */
    *nrounds = nk + 6;
    unsigned char W[240];                     /* 60 words max */
    memcpy(W, key, (size_t)(4 * nk));
    int i = nk;
    unsigned char rcon = 1;
    while (i < words) {
        unsigned char t[4];
        memcpy(t, W + 4 * (i - 1), 4);
        if (i % nk == 0) {
            /* RotWord + SubWord + Rcon */
            unsigned char tmp = t[0];
            t[0] = ZAES_SBOX[t[1]] ^ rcon;
            t[1] = ZAES_SBOX[t[2]];
            t[2] = ZAES_SBOX[t[3]];
            t[3] = ZAES_SBOX[tmp];
            rcon = (unsigned char)((rcon << 1) ^ ((rcon & 0x80) ? 0x1b : 0));
        } else if (nk > 6 && i % nk == 4) {
            t[0] = ZAES_SBOX[t[0]]; t[1] = ZAES_SBOX[t[1]];
            t[2] = ZAES_SBOX[t[2]]; t[3] = ZAES_SBOX[t[3]];
        }
        for (int j = 0; j < 4; j++) { W[4 * i + j] = W[4 * (i - nk) + j] ^ t[j]; }
        i = i + 1;
    }
    memcpy(rk, W, (size_t)(4 * words));
}

static void zaes_encrypt_block(const unsigned char *rk, int nr,
                               const unsigned char in[16], unsigned char out[16]) {
    unsigned char s[16], t[16];
    for (int i = 0; i < 16; i++) { s[i] = in[i] ^ rk[i]; }
    for (int round = 1; round <= nr; round++) {
        const unsigned char *cur = rk + 16 * round;
        /* SubBytes + ShiftRows (state is column-major: s[c*4+r]) */
        t[0]  = ZAES_SBOX[s[0]];  t[1]  = ZAES_SBOX[s[5]];
        t[2]  = ZAES_SBOX[s[10]]; t[3]  = ZAES_SBOX[s[15]];
        t[4]  = ZAES_SBOX[s[4]];  t[5]  = ZAES_SBOX[s[9]];
        t[6]  = ZAES_SBOX[s[14]]; t[7]  = ZAES_SBOX[s[3]];
        t[8]  = ZAES_SBOX[s[8]];  t[9]  = ZAES_SBOX[s[13]];
        t[10] = ZAES_SBOX[s[2]];  t[11] = ZAES_SBOX[s[7]];
        t[12] = ZAES_SBOX[s[12]]; t[13] = ZAES_SBOX[s[1]];
        t[14] = ZAES_SBOX[s[6]];  t[15] = ZAES_SBOX[s[11]];
        if (round < nr) {
            /* MixColumns (xtime = *2 in GF(2^8)) */
            for (int c = 0; c < 4; c++) {
                unsigned char a0 = t[4 * c], a1 = t[4 * c + 1];
                unsigned char a2 = t[4 * c + 2], a3 = t[4 * c + 3];
                unsigned char x01 = (unsigned char)((a0 << 1) ^ ((a0 & 0x80) ? 0x1b : 0));
                unsigned char x11 = (unsigned char)((a1 << 1) ^ ((a1 & 0x80) ? 0x1b : 0));
                unsigned char x21 = (unsigned char)((a2 << 1) ^ ((a2 & 0x80) ? 0x1b : 0));
                unsigned char x31 = (unsigned char)((a3 << 1) ^ ((a3 & 0x80) ? 0x1b : 0));
                /* 2*a0 ^ 3*a1 ^ a2 ^ a3 (3*x = x ^ 2*x) */
                unsigned char t3 = (unsigned char)(a1 ^ x11);
                unsigned char t0 = (unsigned char)(a0 ^ x01);
                unsigned char t2 = (unsigned char)(a2 ^ x21);
                unsigned char t4 = (unsigned char)(a3 ^ x31);
                s[4 * c]     = (unsigned char)(x01 ^ t3 ^ a2 ^ a3);
                s[4 * c + 1] = (unsigned char)(a0 ^ x11 ^ t2 ^ a3);
                s[4 * c + 2] = (unsigned char)(a0 ^ a1 ^ x21 ^ t4);
                s[4 * c + 3] = (unsigned char)(t0 ^ a1 ^ a2 ^ x31);
            }
        } else {
            memcpy(s, t, 16);
        }
        for (int i = 0; i < 16; i++) { s[i] ^= cur[i]; }
    }
    memcpy(out, s, 16);
}

typedef struct {
    unsigned char rk[240];
    int nr;
    unsigned char H[16];        /* GHASH key E(K, 0^16) */
    unsigned char Y[16];        /* GHASH accumulator */
    unsigned char part[16];     /* partial block buffer */
    int pfill;                  /* bytes buffered in part */
    int in_ct;                  /* AAD->ciphertext transition seen (pad A first) */
    unsigned char iv[12];
    int ivlen;
    unsigned char tag[16];
    int have_tag;
    int enc;
    int keybits;
    int ready;
    unsigned long long alen, clen;
    unsigned long long ks_idx;  /* cached keystream block */
    unsigned char ks_cache[16];
    int ks_valid;
} zan_gcm_ctx;

static void zghash_mul(unsigned char *X, const unsigned char *H) {
    unsigned char Z[16] = {0};
    unsigned char V[16];
    memcpy(V, H, 16);
    for (int i = 0; i < 128; i++) {
        if (X[i / 8] & (0x80 >> (i % 8))) {
            for (int j = 0; j < 16; j++) { Z[j] ^= V[j]; }
        }
        int lsb = V[15] & 1;
        for (int j = 15; j > 0; j--) { V[j] = (unsigned char)((V[j] >> 1) | (V[j - 1] << 7)); }
        V[0] >>= 1;
        if (lsb) { V[0] ^= 0xE1; }
    }
    memcpy(X, Z, 16);
}

/* feed bytes into the GHASH stream; zero-pads a buffered partial block when
 * the AAD->ciphertext transition happens (SP 800-38D processes A then C) */
static void zghash_push(zan_gcm_ctx *g, const unsigned char *b, long long n, int is_ct) {
    if (is_ct && !g->in_ct && g->pfill > 0) {
        memset(g->part + g->pfill, 0, (size_t)(16 - g->pfill));
        for (int j = 0; j < 16; j++) { g->Y[j] ^= g->part[j]; }
        zghash_mul(g->Y, g->H);
        g->pfill = 0;
    }
    if (is_ct) { g->in_ct = 1; }
    for (long long i = 0; i < n; i++) {
        g->part[g->pfill++] = b[i];
        if (g->pfill == 16) {
            for (int j = 0; j < 16; j++) { g->Y[j] ^= g->part[j]; }
            zghash_mul(g->Y, g->H);
            g->pfill = 0;
        }
    }
}

/* E(K, inc32(J0, blkidx)) into ks — J0 ends in 0x00000001 and message
 * block i (0-based) uses counter J0 + i + 1 */
static void zgcm_ks_block(zan_gcm_ctx *g, unsigned long long blkidx, unsigned char ks[16]) {
    unsigned char ctr[16] = {0};
    memcpy(ctr, g->iv, (size_t)g->ivlen);
    unsigned int last = (unsigned int)((blkidx + 2) & 0xFFFFFFFFull);
    ctr[12] = (unsigned char)(last >> 24);
    ctr[13] = (unsigned char)(last >> 16);
    ctr[14] = (unsigned char)(last >> 8);
    ctr[15] = (unsigned char)(last);
    zaes_encrypt_block(g->rk, g->nr, ctr, ks);
}

/* final tag = MSB16(GCTR(K, J0, GHASH(A||C||lens))) */
static void zgcm_compute_tag(zan_gcm_ctx *g, unsigned char out[16]) {
    if (g->pfill > 0) {
        memset(g->part + g->pfill, 0, (size_t)(16 - g->pfill));
        for (int j = 0; j < 16; j++) { g->Y[j] ^= g->part[j]; }
        zghash_mul(g->Y, g->H);
        g->pfill = 0;
    }
    unsigned char lens[16] = {0};
    unsigned long long abits = g->alen * 8, cbits = g->clen * 8;
    for (int i = 0; i < 8; i++) { lens[i] = (unsigned char)(abits >> (56 - 8 * i)); }
    for (int i = 0; i < 8; i++) { lens[8 + i] = (unsigned char)(cbits >> (56 - 8 * i)); }
    for (int j = 0; j < 16; j++) { g->Y[j] ^= lens[j]; }
    zghash_mul(g->Y, g->H);
    unsigned char j0[16] = {0};
    memcpy(j0, g->iv, (size_t)g->ivlen);
    j0[15] = 1;                                  /* J0 for 96-bit IV */
    unsigned char tk[16];
    zaes_encrypt_block(g->rk, g->nr, j0, tk);
    for (int j = 0; j < 16; j++) { out[j] = (unsigned char)(g->Y[j] ^ tk[j]); }
}

void *EVP_CIPHER_CTX_new(void) {
    zan_gcm_ctx *g = (zan_gcm_ctx *)calloc(1, sizeof(zan_gcm_ctx));
    return g;
}
void EVP_CIPHER_CTX_free(void *ctx) { free(ctx); }

static const char zgc128[] = "AES-128-GCM";
static const char zgc192[] = "AES-192-GCM";
static const char zgc256[] = "AES-256-GCM";
const char *EVP_aes_128_gcm(void) { return zgc128; }
const char *EVP_aes_192_gcm(void) { return zgc192; }
const char *EVP_aes_256_gcm(void) { return zgc256; }

/* two-phase init: phase 1 names the cipher, phase 2 (SET_IVLEN between)
 * carries key+iv; the wrapper always uses 12-byte IVs */
static int zgcm_init(void *ctx, const char *cipher, const unsigned char *key,
                     const unsigned char *iv, int enc) {
    zan_gcm_ctx *g = (zan_gcm_ctx *)ctx;
    if (!g) { return 0; }
    if (cipher) {
        if (memcmp(cipher, zgc128, 11) == 0) { g->keybits = 128; }
        else if (memcmp(cipher, zgc192, 11) == 0) { g->keybits = 192; }
        else if (memcmp(cipher, zgc256, 11) == 0) { g->keybits = 256; }
        else { return 0; }
        g->enc = enc;
        return 1;
    }
    if (!key || !iv) { return 0; }
    if (g->ivlen != 12) { return 0; }            /* J0 shortcut needs 96-bit IVs */
    zaes_expand_key(key, g->keybits, g->rk, &g->nr);
    unsigned char zero[16] = {0};
    zaes_encrypt_block(g->rk, g->nr, zero, g->H);
    memset(g->Y, 0, 16);
    g->pfill = 0; g->in_ct = 0;
    g->alen = 0; g->clen = 0; g->have_tag = 0;
    memcpy(g->iv, iv, 12);
    g->ivlen = 12;
    g->ready = 1;
    return 1;
}
int EVP_EncryptInit_ex(void *ctx, const char *cipher, const void *impl,
                       const unsigned char *key, const unsigned char *iv) {
    (void)impl;
    return zgcm_init(ctx, cipher, key, iv, 1);
}
int EVP_DecryptInit_ex(void *ctx, const char *cipher, const void *impl,
                       const unsigned char *key, const unsigned char *iv) {
    (void)impl;
    return zgcm_init(ctx, cipher, key, iv, 0);
}

/* GCM is a stream: message byte at offset o uses keystream block o/16,
 * byte o%16 — updates at any offset compose transparently */
static int zgcm_update(zan_gcm_ctx *g, unsigned char *out, int *outl,
                       const unsigned char *in, long long inl) {
    if (!g || !g->ready || inl < 0 || (!in && inl > 0)) { return 0; }
    if (out == NULL) {                            /* AAD */
        zghash_push(g, in, inl, 0);
        g->alen += (unsigned long long)inl;
        if (outl) { *outl = (int)inl; }
        return 1;
    }
    unsigned long long base = g->clen;
    unsigned char ks[16];
    for (long long i = 0; i < inl; i++) {
        unsigned long long o = base + (unsigned long long)i;
        unsigned long long blk = o / 16;
        if (!g->ks_valid || g->ks_idx != blk) {
            zgcm_ks_block(g, blk, g->ks_cache);
            g->ks_idx = blk;
            g->ks_valid = 1;
        }
        memcpy(ks, g->ks_cache, 16);
        unsigned char c = (unsigned char)(in[i] ^ ks[o % 16]);
        out[i] = c;
        /* GHASH always runs over the CIPHERTEXT: on encrypt that is what we
         * produce, on decrypt it is the input itself */
        unsigned char ctb = g->enc ? c : in[i];
        zghash_push(g, &ctb, 1, 1);
    }
    g->clen += (unsigned long long)inl;
    if (outl) { *outl = (int)inl; }
    return 1;
}
int EVP_EncryptUpdate(void *ctx, unsigned char *out, int *outl,
                      const unsigned char *in, int inl) {
    return zgcm_update((zan_gcm_ctx *)ctx, out, outl, in, (long long)inl);
}
int EVP_DecryptUpdate(void *ctx, unsigned char *out, int *outl,
                      const unsigned char *in, int inl) {
    return zgcm_update((zan_gcm_ctx *)ctx, out, outl, in, (long long)inl);
}
int EVP_EncryptFinal_ex(void *ctx, unsigned char *out, int *outl) {
    (void)ctx; (void)out;
    if (outl) { *outl = 0; }
    return 1;
}
int EVP_DecryptFinal_ex(void *ctx, unsigned char *out, int *outl) {
    (void)out;
    if (outl) { *outl = 0; }
    zan_gcm_ctx *g = (zan_gcm_ctx *)ctx;
    if (!g || !g->ready || !g->have_tag) { return 0; }
    unsigned char t[16];
    zgcm_compute_tag(g, t);
    unsigned char diff = 0;
    for (int i = 0; i < 16; i++) { diff |= (unsigned char)(t[i] ^ g->tag[i]); }
    return diff == 0 ? 1 : 0;
}
int EVP_CIPHER_CTX_ctrl(void *ctx, int type, int arg, void *ptr) {
    zan_gcm_ctx *g = (zan_gcm_ctx *)ctx;
    if (!g) { return 0; }
    if (type == 0x9) {                            /* SET_IVLEN */
        if (arg <= 0 || arg > 12) { return 0; }
        g->ivlen = arg;
        return 1;
    }
    if (type == 0x10) {                           /* GET_TAG */
        if (!ptr || arg != 16) { return 0; }
        unsigned char t[16];
        zgcm_compute_tag(g, t);
        memcpy(ptr, t, 16);
        return 1;
    }
    if (type == 0x11) {                           /* SET_TAG */
        if (!ptr || arg != 16) { return 0; }
        memcpy(g->tag, ptr, 16);
        g->have_tag = 1;
        return 1;
    }
    return 0;
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
