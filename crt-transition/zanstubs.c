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
void *GetString(void *p, int off, int len) {
    (void)p; (void)off; (void)len;
    fprintf(stderr, "zanstub: GetString not available\n");
    abort();
}
int Crc32(void *p, int len) {
    (void)p; (void)len;
    fprintf(stderr, "zanstub: Crc32 not available\n");
    abort();
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
