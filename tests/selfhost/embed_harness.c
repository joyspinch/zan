/* b37 differential harness: oracle zan_inflate.c over the same vectors the
 * Zan probe (embed_edges.zan) feeds MY runtime. The oracle install ships no
 * zan_inflate.o beside zanc, so a Zan probe cannot link the decoder there —
 * this harness over the oracle source is the reference golden instead.
 * Output lines are identical to the probe's: "<name> rawlen <n>",
 * "<name> decode <fnv1a-64 hex>" or "<name> decode null".
 * build: cc -I<oracle>/src/common -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZIP_APIS
 *          -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITERS
 *          embed_harness.c -o embed_harness */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "/Users/qq/Desktop/zanlang/zan-lang/src/common/miniz.c"
#include "/Users/qq/Desktop/zanlang/zan-lang/src/common/miniz_tinfl.c"
#include "/Users/qq/Desktop/zanlang/zan-lang/src/common/miniz_tdef.c"
#include "/Users/qq/Desktop/zanlang/zan-lang/src/runtime/zan_inflate.c"

#define TAG 0x4000000000000000ULL

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

static uint8_t *buf_of(const char *hex, int *outN) {
    int n = (int)strlen(hex) / 2;
    uint8_t *p = (uint8_t *)malloc((size_t)n + 1);
    for (int i = 0; i < n; i++)
        p[i] = (uint8_t)(hexval(hex[2 * i]) * 16 + hexval(hex[2 * i + 1]));
    *outN = n;
    return p;
}

static void fnv(const uint8_t *p, size_t n, char out[17]) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 1099511628211ULL;
    snprintf(out, 17, "%016llx", (unsigned long long)h);
}

static void good(const char *name, const char *hex, int r) {
    int n;
    uint8_t *p = buf_of(hex, &n);
    printf("%s rawlen %u\n", name,
           (unsigned)zan_embed_rawlen(p, TAG | (uint64_t)n));
    uint8_t *outp = (uint8_t *)zan_embed_decode(p, TAG | (uint64_t)n);
    if (!outp) {
        printf("%s decode null\n", name);
    } else {
        char d[17];
        fnv(outp, (size_t)r + 1, d);
        printf("%s decode %s\n", name, d);
        free(outp);
    }
    free(p);
}

static void bad(const char *name, const char *hex, int passLen, int tag) {
    int n;
    uint8_t *p = buf_of(hex, &n);
    uint64_t len = (uint64_t)passLen;
    if (tag) len |= TAG;
    uint8_t *outp = (uint8_t *)zan_embed_decode(p, len);
    printf("%s rawlen %u decode %s\n", name,
           (unsigned)zan_embed_rawlen(p, len), outp ? "NONNULL" : "null");
    free(outp);
    free(p);
}

int main(void) {
    good("hello3",  "120000000a000000cb48cdc9c957c8402201", 18);
    good("empty",   "00000000020000000300", 0);
    good("bytes256","00040000180100006360646266616563e7e0e4e2e6e1e5e3171014121611151397909492969195935750545256515553d7d0d4d2d6d1d5d33730343236313533b7b0b4b2b6b1b5b37770747276717573f7f0f4f2f6f1f5f30f080c0a0e090d0b8f888c8a8e898d8b4f484c4a4e494d4bcfc8cccacec9cdcb2f282c2a2e292d2bafa8acaaaea9adab6f686c6a6e696d6befe8eceaeee9edeb9f3071d2e42953a74d9f3173d6ec3973e7cd5fb070d1e2254b972d5fb172d5ea356bd7addfb071d3e62d5bb76ddfb173d7ee3d7bf7ed3f70f0d0e123478f1d3f71f2d4e93367cf9dbf70f1d2e52b57af5dbf71f3d6ed3b77efdd7ff0f0d1e3274f9f3d7ff1f2d5eb376fdfbdfff0f1d3e72f5fbf7dfff1f3d7ef3f7ffffd6718f5ffa8ff47b0ff01", 1024);
    good("bigtext", "35070000820000002bc94855282ccd4cce56482aca2fcf5348cbaf50c82acd2d2856c82f4b2d5228014ae72456552aa4e4a7eb8179a38a47158f2aa6aae297b3b73d5db7f7e9dae9cfd72c7bb2a3efc98eb5cfa6b53fdbbefd59df248594d4b49cc4925485a77dddcff7ac7cb1b5e5d9ae09cfb676bf583f15a466ce9ac70d4da3da47b58f6aa7a77600", 1845);
    good("rand300", "2c01000031010000012c01d3fe38b4e652e44da7f2370d9e260e27136550a4a3a6d07f5c0c332f8b1224083fd22b902f8911e81818f8c99d5d5d9831957504d90e945de2e8f54ee781cc75f636d85099095aa300165a67036f9b540d6b8f0be21124179c3dd9f73817ce6e118d264aad6cb6dd210faf94acd3cf92c190237cb11f5d108cf25930263938b370a1b5769fa0f1483f95a90d9df2f130d60fcf04bd93f50ae69514da8c659ce2b10cccdaebf990d19838b0d7ec0b3e97818ecb96c4dbadbe172296d5234a42b24c6ba4e6ed24ec636a8ac0a1271e5866279238aaf84e58056d8f2fa8edd094ba97ae8b15442ee2db611a91bfe39469733a9247d58fa3c55018300372555fd235f11829fb388c22e44cb637f01210c3707a90b405420fb169779edfb5b9342405157f54b12eae62d11e887eb0766d", 300);
    bad("untagged",    "120000000a000000cb48cdc9c957c8402201", 18, 0);
    bad("totalshort",  "120000000a000000cb48cdc9c957c8402201", 4, 1);
    bad("compleng",    "1200000013000000cb48cdc9c957c8402201", 18, 1);
    bad("trunc",       "1200000005000000cb48cdc9c957c8402201", 14, 1);
    bad("rawsmall",    "110000000a000000cb48cdc9c957c8402201", 17, 1);
    bad("emptystream", "1200000000000000", 8, 1);
    bad("rawlenonly",  "35070000820000002bc94855282ccd4cce56482aca2fcf5348cbaf50c82acd2d2856c82f4b2d5228014ae72456552aa4e4a7eb8179a38a47158f2aa6aae297b3b73d5db7f7e9dae9cfd72c7bb2a3efc98eb5cfa6b53fdbbefd59df248594d4b49cc4925485a77dddcff7ac7cb1b5e5d9ae09cfb676bf583f15a466ce9ac70d4da3da47b58f6aa7a77600", 8, 1);
    return 0;
}
