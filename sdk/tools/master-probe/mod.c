/* Master Audio Probe (diagnostic, read-only).
 *
 * Question: does EP136's USB-recording processor (meow::AudioRecorderUnit) receive the
 * final master mix, and does it run when nothing is being recorded?
 *
 * It swaps AudioRecorderUnit's processBlock(AudioBuffer<float>&, MidiBuffer&) vtable slot
 * (vtable 0x29ace60, slot 7, stock 0x1235e00) for a wrapper that calls the original first
 * and then only READS the buffer: call rate, channel count, block size and per-channel
 * level. The audio is never changed. The wrapper takes no locks and does no I/O; tick()
 * turns the counters into status lines and a log line every 10 s.
 *
 * JUCE AudioBuffer<float> (6.x): int numChannels @0, int size @4, float **channels @16. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "oqmod.h"

#define SLOT 0x29ace98ul      /* 0x29ace60 + 7*8 */
#define STOCK 0x1235e00ul
#define CH_MAX 8

static const struct oqmod_api *api;
static void (*original)(void *, void *, void *);
static int installed, on;
/* Per processor instance (EP136 may run more than one recorder unit). */
#define INST 4
struct inst {
    void *self; uint64_t calls, samples; uint32_t channels, block;
    uint32_t peak_q[CH_MAX]; uint64_t sumsq_q[CH_MAX]; uint64_t window;
};
static struct inst inst[INST];
static volatile uint32_t overflow;

static struct inst *find(void *self) {
    for (int i = 0; i < INST; i++) {
        void *s = __atomic_load_n(&inst[i].self, __ATOMIC_ACQUIRE);
        if (s == self) return &inst[i];
        if (!s) {
            void *expected = NULL;
            if (__atomic_compare_exchange_n(&inst[i].self, &expected, self, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return &inst[i];
            if (expected == self) return &inst[i];
        }
    }
    overflow++;
    return NULL;
}

static void process(void *self, void *buffer, void *midi) {
    original(self, buffer, midi);
    if (!buffer) return;
    struct inst *m = find(self);
    if (!m) return;
    int n_ch = *(const int *)buffer, n = *(const int *)((const char *)buffer + 4);
    float *const *ch = *(float *const *const *)((const char *)buffer + 16);
    m->calls++;
    if (n_ch < 1 || n_ch > 64 || n < 1 || n > 16384 || !ch) return;
    m->samples += (uint64_t)n; m->channels = (uint32_t)n_ch; m->block = (uint32_t)n;
    if (!on) return;
    for (int c = 0; c < n_ch && c < CH_MAX; c++) {
        const float *p = ch[c];
        if (!p) continue;
        uint32_t peak = 0; uint64_t sum = 0;
        for (int i = 0; i < n; i += 4) {          /* every 4th sample is enough for a level */
            float v = p[i]; if (v < 0) v = -v; if (v > 4.f) v = 4.f;
            uint32_t q = (uint32_t)(v * 65536.f);
            if (q > peak) peak = q;
            sum += ((uint64_t)q * q) >> 16;
        }
        if (peak > m->peak_q[c]) m->peak_q[c] = peak;
        m->sumsq_q[c] += sum;
    }
    m->window += (uint64_t)((n + 3) / 4);
}

static double db(double linear) { return linear > 1e-6 ? 20.0 * log10(linear) : -120.0; }

static void tick(void) {
    static uint64_t last_ms, last_calls[INST], last_samples[INST];
    uint64_t now = api->now_ms();
    if (!last_ms) { last_ms = now; return; }
    if (now - last_ms < 2000) return;
    double secs = (now - last_ms) / 1000.0;
    last_ms = now;
    char text[900]; int at = snprintf(text, sizeof text, "%s%s", installed ? "Hook installed (read-only)" : "Hook NOT installed",
                                      overflow ? ", more than 4 units" : "");
    char logline[200]; int lat = 0; logline[0] = 0;
    for (int i = 0; i < INST; i++) {
        struct inst *m = &inst[i];
        if (!m->self) continue;
        uint64_t c = m->calls, s = m->samples;
        unsigned cps = (unsigned)((c - last_calls[i]) / secs), sps = (unsigned)((s - last_samples[i]) / secs);
        last_calls[i] = c; last_samples[i] = s;
        uint64_t w = m->window ? m->window : 1;
        double l = sqrt((double)m->sumsq_q[0] / (double)w / 65536.0), r = sqrt((double)m->sumsq_q[1] / (double)w / 65536.0);
        double lp = m->peak_q[0] / 65536.0, rp = m->peak_q[1] / 65536.0;
        if (at < (int)sizeof text - 120)
            at += snprintf(text + at, sizeof text - at, "\nUnit %d: %u Hz, %u ch, block %u, L %.0f dB (peak %.0f), R %.0f dB (peak %.0f)",
                           i + 1, sps, m->channels, m->block, db(l), db(lp), db(r), db(rp));
        if (lat < (int)sizeof logline - 60)
            lat += snprintf(logline + lat, sizeof logline - lat, "%su%d %uHz %ub/s L%.0f/%.0f R%.0f/%.0f", lat ? "; " : "",
                            i + 1, sps, cps, db(l), db(lp), db(r), db(rp));
        for (int k = 0; k < CH_MAX; k++) { m->peak_q[k] = 0; m->sumsq_q[k] = 0; }
        m->window = 0;
    }
    api->set_status(text);
    static unsigned n; if (!(n++ % 5) && logline[0]) api->log(logline);   /* about every 10 s */
}
static void enabled(int yes) {
    on = yes;
    if (yes && !installed) {
        if (!api->ep136_ok()) { api->set_status("Not this deck software build."); on = 0; return; }
        installed = api->patch(SLOT, STOCK, (void *)process, (void **)&original);
        if (!installed) api->set_status("Could not attach (slot already changed?).");
    }
}

int oqmod_init(const struct oqmod_api *a, struct oqmod_info *info) {
    if (!a || a->version < OQMOD_API_VERSION || a->size < sizeof *a) return 1;
    api = a;
    info->api_version = OQMOD_API_VERSION;
    info->name = "Master Audio Probe";
    info->description = "Diagnostic: reads the audio of the USB recording path\nand shows levels. Changes no audio.";
    info->version = "0.2";
    info->tick = tick;
    info->enabled = enabled;
    return 0;
}
