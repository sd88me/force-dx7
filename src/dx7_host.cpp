/* dx7_host.cpp — Force/MockbaMod runtime host for the ported Dexed (DX7/MSFA)
 * DSP synth. Same porting pattern as force-jv880/src/jv_host.cpp and
 * force-maze/src/maze_host.cpp — plays the role Move's chain host plays for
 * dx7_plugin.cpp:
 *
 *   Move/Schwung host                     this shim
 *   -------------------------------------- --------------------------------
 *   dlopen(dsp.so), move_plugin_init_v2    links dx7_plugin.o + msfa/*.cc
 *                                          directly, calls it directly
 *   on_midi() per incoming note            RtMidi input callback -> on_midi()
 *   render_block() per SPI-callback block  wall-clock timer thread -> render_block()
 *   set_param(key, "64") from a knob       a local control socket, or CC on
 *                                          the control channel -> set_param()
 *   int16 stereo out via the mailbox       float32 into ForceAudioIn's shared-
 *                                          memory ring (forceAudioInject.h)
 *
 * dx7_plugin.cpp + msfa/*.cc are vendored VERBATIM from schwung-dx7 — unlike
 * force-jv880's jv880_plugin.cpp, this one needed ZERO edits for the Force
 * port: no SCHED_FIFO anywhere in it (confirmed by reading it), and its one
 * NEON-optimized path (msfa/fm_op_kernel.cc) is gated behind an explicit,
 * project-defined HAVE_NEON macro rather than the compiler's automatic
 * __ARM_NEON (which is what bit force-jv880's resampler on armv7) — simply
 * not defining HAVE_NEON in the build (see scripts/build.sh) skips that path
 * entirely and falls back to the portable scalar kernel, with no portability
 * bug to fix. create_instance() also scans banks/ synchronously (no
 * background load thread the way jv880_plugin.cpp's does), so there's no
 * loading_complete race to poll for either — chain_params is available
 * immediately after create_instance returns.
 *
 * Build: see scripts/build.sh (native armhf under QEMU, links -lasound
 * -lpthread -lrt, same toolchain as force-jv880/force-maze).
 */

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "rtmidi/RtMidi.h"
#include "plugin_api_v1.h"
#include "forceAudioInject.h"

extern "C" plugin_api_v2_t *move_plugin_init_v2(const void *host);

/* ---------------------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------------------- */
static std::atomic<bool> g_run{true};
static std::mutex        g_lock;
static plugin_api_v2_t  *g_api  = nullptr;
static void             *g_inst = nullptr;

static ai_shm_t *g_shm_in = nullptr;   /* Audio-In 1/2 ring (AI_SHM_NAME_FMT) */
static ai_shm_t *g_shm_out = nullptr;  /* Out 3/4 ring (AI_SHM_NAME_FMT_OUT) */
static std::atomic<uint64_t> g_ring_drops{0};

static std::string g_chain_params_json;
static std::string g_ctrl_sock_path = "/tmp/dx7_ctrl.sock";
static bool         g_verbose = false;
/* Diagnostic only: bypasses dx7_plugin.cpp/MSFA entirely and writes a pure
 * 440Hz sine straight into the ring, to isolate whether audible grit/
 * distortion is in this shim's ring/mixing pipeline or in the DX7 render
 * path itself -- see DESIGN.md's writeup of this specific investigation. */
static bool         g_test_tone = false;
static unsigned     g_mix_slot = 2;   /* 0 used by force-maze, 1 by force-jv880 (see their own headers) */

/* ---------------------------------------------------------------------------
 * CC -> set_param, for a Force Q-Link-mapped MIDI track. 16 of Dexed's 20
 * chain_params (module.json) fit one Q-Link bank; lfo_delay/lfo_pms/lfo_sync/
 * transpose (a fixed middle-C reference note, distinct from octave_transpose)
 * are web-panel-only, matching Move's own module.json "knobs" curation
 * philosophy (it only picks 5 for Move's few hardware knobs) extended to
 * the Force's 16-knob Q-Link bank. All of Dexed's chain_params are plain
 * integers (v2_set_param uses atoi() throughout, confirmed by reading it),
 * same as force-jv880's — one PARAMS kind, no float/log/momentary needed.
 * ------------------------------------------------------------------------- */
struct ParamSpec {
    const char *key;
    int         lo, hi;
    int         cc;
};
static const ParamSpec PARAMS[] = {
    { "preset",           0,  31, 20 },
    { "output_level",     0, 100, 21 },
    { "octave_transpose",-3,   3, 22 },
    { "algorithm",        1,  32, 23 },
    { "feedback",         0,   7, 24 },
    { "osc_sync",         0,   1, 25 },
    { "lfo_speed",        0,  99, 26 },
    { "lfo_pmd",          0,  99, 27 },
    { "lfo_amd",          0,  99, 28 },
    { "lfo_wave",         0,   5, 29 },
    { "op1_level",        0,  99, 30 },
    { "op2_level",        0,  99, 31 },
    { "op3_level",        0,  99, 32 },
    { "op4_level",        0,  99, 33 },
    { "op5_level",        0,  99, 34 },
    { "op6_level",        0,  99, 35 },
};
static const int N_PARAMS = (int)(sizeof(PARAMS) / sizeof(PARAMS[0]));
static std::unordered_map<int, int> g_cc2param;
static int g_ctrl_ch = 0;

static void apply_cc(int idx, int value /* 0..127 */) {
    const ParamSpec &p = PARAMS[idx];
    int v = p.lo + (int)std::lround((p.hi - p.lo) * (value / 127.0));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", v);
    { std::lock_guard<std::mutex> lk(g_lock); g_api->set_param(g_inst, p.key, buf); }
    if (g_verbose) fprintf(stderr, "[dx7] cc %d -> %s = %s\n", p.cc, p.key, buf);
}

/* ---------------------------------------------------------------------------
 * Shared-memory ring setup (producer side). Dexed is audio_out-only, same
 * shape as force-jv880/force-maze's ring use. Default slot 2 (0=maze,
 * 1=jv880 on this device — see NSMODULE.json if that ever needs changing).
 * ------------------------------------------------------------------------- */
static char g_shm_name_in[24];
static char g_shm_name_out[24];

/* Default headroom below unity: dx7_plugin.cpp's own render path hard-clips
 * at int16 full-scale (confirmed by reading v2_render_block -- proper
 * saturation, no wraparound bug, but a real ceiling), and forceAudioJack.so
 * mixes this ADDITIVELY on top of whatever else is already on the target
 * bus, which can push an already-hot signal over that ceiling a second
 * time. Leaves margin by default; raise via "mix.gain" if a patch is quiet. */
static constexpr float DEFAULT_GAIN = 0.6f;

/* Opens/creates one ring at `name`, zeroed and stamped with the given
 * initial mix state, magic published last. Shared by the in-bus and
 * out-bus setup below -- same struct, different shm namespace. */
static ai_shm_t *shm_open_ring(const char *name, uint32_t enabled, uint32_t channel_mask) {
    shm_unlink(name);
    int fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { perror("shm_open"); return nullptr; }
    if (ftruncate(fd, AI_SHM_BYTES) != 0) { perror("ftruncate"); close(fd); return nullptr; }
    void *m = mmap(nullptr, AI_SHM_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) { perror("mmap"); return nullptr; }

    ai_shm_t *shm = (ai_shm_t *)m;
    memset(shm, 0, AI_SHM_BYTES);
    shm->rate = (uint32_t)MOVE_SAMPLE_RATE;
    shm->channels = 2;
    shm->enabled = enabled;
    shm->gain = DEFAULT_GAIN;
    shm->channel_mask = channel_mask;
    __atomic_store_n(&shm->magic, AI_MAGIC, __ATOMIC_RELEASE);
    return shm;
}

/* Both rings are always created: the producer always renders into both (see
 * ring_push), and "mix.dest" just flips which ring is `enabled` (mixed by
 * forceAudioJack.so) -- same host-level mute contract as a single-ring
 * voice, just applied per-destination. Default destination is IN1,2. */
static bool shm_setup() {
    ai_shm_name(g_mix_slot, g_shm_name_in, sizeof(g_shm_name_in));
    ai_shm_name_out(g_mix_slot, g_shm_name_out, sizeof(g_shm_name_out));
    g_shm_in  = shm_open_ring(g_shm_name_in, 1, AI_CHAN_LR);
    g_shm_out = shm_open_ring(g_shm_name_out, 0, AI_CHAN_LR);
    return g_shm_in && g_shm_out;
}

static void ring_push_one(ai_shm_t *shm, const float *interleaved, uint32_t frames) {
    if (!shm) return;
    uint32_t head = shm->head;
    uint32_t tail = __atomic_load_n(&shm->tail, __ATOMIC_ACQUIRE);
    uint32_t space = (AI_RING_FRAMES - 1) - ((head - tail) & (AI_RING_FRAMES - 1));

    uint32_t take = frames;
    if (take > space) { take = space; g_ring_drops++; }
    for (uint32_t i = 0; i < take; i++) {
        uint32_t fr = (head + i) & (AI_RING_FRAMES - 1);
        float *dst = &shm->ring[(size_t)fr * AI_MAX_CH];
        dst[0] = interleaved[2 * i];
        dst[1] = interleaved[2 * i + 1];
    }
    __atomic_store_n(&shm->head, (head + take) & (AI_RING_FRAMES - 1), __ATOMIC_RELEASE);
    shm->frames_written += take;
}

/* Renders into both rings every tick regardless of which one is currently
 * `enabled` -- same "still drains while muted" contract as a single-ring
 * voice (see forceAudioInject.h), just applied to whichever ring isn't the
 * active mix.dest right now, so switching destinations never resumes from a
 * stale backlog. */
static void ring_push(const float *interleaved, uint32_t frames) {
    ring_push_one(g_shm_in, interleaved, frames);
    ring_push_one(g_shm_out, interleaved, frames);
}

/* ---------------------------------------------------------------------------
 * RtMidi input — notes/pitch-bend/aftertouch/sustain pass straight through
 * to on_midi (see module.json: aftertouch, sustain CC64, pitch bend all
 * natively supported by dx7_plugin.cpp), Control Change on the control
 * channel is intercepted for the Q-Link CC table above.
 * ------------------------------------------------------------------------- */
static void on_midi_cb(double /*dt*/, std::vector<unsigned char> *msg, void * /*ud*/) {
    if (!msg || msg->empty()) return;
    const uint8_t *b = msg->data();
    size_t len = msg->size();
    uint8_t status = b[0];
    uint8_t type = status & 0xF0;
    uint8_t chan = status & 0x0F;

    if (type == 0xB0 && len >= 3 && chan == (uint8_t)g_ctrl_ch) {
        auto it = g_cc2param.find(b[1]);
        if (it != g_cc2param.end()) apply_cc(it->second, b[2]);
        return;
    }

    std::lock_guard<std::mutex> lk(g_lock);
    g_api->on_midi(g_inst, b, (int)len, 0 /* MOVE_MIDI_SOURCE_INTERNAL */);
}

/* ---------------------------------------------------------------------------
 * Timer thread — dx7_plugin.cpp's render_block is a synchronous render
 * (unlike jv880_plugin.cpp's own background emu thread + drain), so this
 * loop IS the synth's real-time clock, same as force-maze's maze_host.cpp.
 * Elapsed-real-time frame count, never a fixed period — see maze_host.cpp's
 * own header comment for why (sleep_for() jitter on a plain SCHED_OTHER
 * thread means a fixed cadence silently falls behind real time).
 * ------------------------------------------------------------------------- */
static std::atomic<double>   g_max_wake_ms{0.0};
static std::atomic<uint64_t> g_late_wakes{0};
static std::atomic<uint64_t> g_total_wakes{0};

/* Clock-rate compensation, same constant proven in force-maze/force-jv880's
 * variable-length timer loops. MEASURED 2026-09-17 to be correct (-0.2ppm
 * residual over a 20-minute idle run) for THIS host's own variable-length
 * loop -- see HANDOFF.md. NOT re-enabled below: an earlier live test of
 * this constant combined with fixed-128-block chunking (see that same
 * HANDOFF.md section) showed the two don't mix -- ring backlog grew
 * unboundedly (~51 frames/sec, ~1160ppm), a much larger and clearly real
 * effect, not measurement noise. Left defined but unused pending a proper
 * remeasurement of drift specifically against the fixed-128 shape. */
constexpr double RATE_CORRECTION = 1.0;

/* Fixed-128-frame-block rendering: dx7_plugin.cpp's grit bug needed
 * render_block() calls quantized to a small constant size rather than the
 * variable, elapsed-time-sized calls the previous version here used.
 * force-dx7 had no git history to recover the original fix from (fixed by
 * git-initing this repo), so this is a fresh implementation of the same
 * idea with the previously-identified pacing bug fixed:
 *
 * The earlier attempt drained however many 128-frame chunks were owed (up
 * to 32 back-to-back) in one unpaced tight loop within a single wake, no
 * sleep between chunks -- that got the ring stuck at a ~4400-frame backlog
 * immediately on every fresh start (forceAudioIn.so's hysteresis-trim
 * never triggered to correct it) and sounded worse than the grit it fixed.
 *
 * This version keeps a fractional "frame debt" owed since the last chunk,
 * accumulated from real elapsed time, but drains it in fixed 128-frame
 * chunks, capped at MAX_CHUNKS_PER_WAKE per wake. Any leftover debt (a
 * long stall, or just not at a full 128 yet) carries into the next wake
 * instead of being forced out immediately, so a startup or scheduler-
 * jitter backlog spreads across several ~1.5ms wake periods instead of one
 * unpaced burst -- the natural sleep_for() gap between wakes IS the
 * pacing, no explicit inter-chunk sleep needed.
 *
 * NOT YET CONFIRMED clean by ear over a long run at time of writing -- a
 * short (65s) earlier test showed backlog settling at an elevated but
 * STABLE ~4400-4500 frames (added latency, not growing/shrinking), but
 * that 65s window is exactly the kind of "too short to trust" measurement
 * the variable-length version's own 15-20 minute startup decay (see
 * HANDOFF.md) turned out to need -- this may simply need more time to
 * settle lower, not be permanently stuck. Re-verify with a long soak
 * before trusting the backlog number alone. */
static void timer_loop() {
    constexpr int BLOCK_FRAMES = 128;
    constexpr int MAX_CHUNKS_PER_WAKE = 8;          /* burst ceiling: 8*128 = 1024 frames (~23ms) per wake */
    constexpr double MAX_DEBT_FRAMES = 4096.0;      /* same ceiling the old variable-length cap used */
    int16_t  pcm[BLOCK_FRAMES * 2];
    float    flt[BLOCK_FRAMES * 2];
    const auto period = std::chrono::microseconds(1500);

    using clock = std::chrono::steady_clock;
    auto prev = clock::now();
    auto last_stat = prev;
    double frame_debt = 0.0;

    while (g_run.load()) {
        std::this_thread::sleep_for(period);
        auto now = clock::now();
        double secs = std::chrono::duration<double>(now - prev).count();
        prev = now;

        double ms = secs * 1000.0;
        double seen_max = g_max_wake_ms.load();
        if (ms > seen_max) g_max_wake_ms.store(ms);
        g_total_wakes++;
        if (ms > 9.0) g_late_wakes++;

        frame_debt += secs * MOVE_SAMPLE_RATE * RATE_CORRECTION;
        if (frame_debt > MAX_DEBT_FRAMES) frame_debt = MAX_DEBT_FRAMES;   /* long-stall ceiling, not a per-wake one */

        for (int chunk = 0; chunk < MAX_CHUNKS_PER_WAKE && frame_debt >= BLOCK_FRAMES; chunk++) {
            if (g_test_tone) {
                static double phase = 0.0;
                const double freq = 440.0, twoPi = 6.283185307179586;
                for (int i = 0; i < BLOCK_FRAMES; i++) {
                    float s = 0.2f * (float)std::sin(phase);
                    flt[i*2] = s; flt[i*2+1] = s;
                    phase += twoPi * freq / MOVE_SAMPLE_RATE;
                    if (phase > twoPi) phase -= twoPi;
                }
            } else {
                {
                    std::lock_guard<std::mutex> lk(g_lock);
                    g_api->render_block(g_inst, pcm, BLOCK_FRAMES);
                }
                for (int i = 0; i < BLOCK_FRAMES * 2; i++) flt[i] = pcm[i] / 32768.0f;
            }
            ring_push(flt, BLOCK_FRAMES);
            frame_debt -= BLOCK_FRAMES;
        }

        if (now - last_stat >= std::chrono::seconds(5)) {
            last_stat = now;
            uint32_t backlog = g_shm_in ? (uint32_t)((g_shm_in->head - __atomic_load_n(&g_shm_in->tail, __ATOMIC_ACQUIRE))
                                                   & (AI_RING_FRAMES - 1))
                                      : 0;
            fprintf(stderr, "[dx7] render thread: max wake gap %.1fms, %llu/%llu wakes > 9ms, ring drops %llu, "
                            "backlog %u frames, frame debt %.0f\n",
                    g_max_wake_ms.load(),
                    (unsigned long long)g_late_wakes.load(), (unsigned long long)g_total_wakes.load(),
                    (unsigned long long)g_ring_drops.load(), backlog, frame_debt);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Control socket — same plain newline-terminated protocol as
 * force-jv880/force-maze's own host shims:
 *
 *   SET <key> <value>\n   -> "OK\n" or "ERR\n"
 *   GET <key>\n           -> "<value>\n" or "ERR\n"
 *   DESCRIBE\n            -> the module's chain_params JSON, one line
 *   NOTE <note> <vel>\n   -> trigger a note (web UI "audition" button)
 * ------------------------------------------------------------------------- */
/* mix.dest indices, in DEST_LABELS order below. */
enum { DEST_IN1 = 0, DEST_IN2 = 1, DEST_OUT3 = 2, DEST_OUT4 = 3, DEST_IN12 = 4, DEST_OUT34 = 5 };
static const char *DEST_LABELS[] = { "IN1", "IN2", "OUT3", "OUT4", "IN1,2", "OUT3,4" };
static int g_dest_idx = DEST_IN12;   /* default: stereo into Audio-In 1,2, matches prior AI_CHAN_LR default */

/* Applies a destination: exactly one of g_shm_in/g_shm_out is `enabled`
 * (mixed by forceAudioJack.so), the other stays attached-but-muted so
 * switching destinations never resumes from a stale backlog (see
 * ring_push). channel_mask picks L/R/both within whichever bus is live. */
static void apply_dest(int idx) {
    g_dest_idx = idx;
    bool is_out = (idx == DEST_OUT3 || idx == DEST_OUT4 || idx == DEST_OUT34);
    g_shm_in->enabled  = is_out ? 0u : 1u;
    g_shm_out->enabled = is_out ? 1u : 0u;
    uint32_t mask = (idx == DEST_IN1 || idx == DEST_OUT3) ? AI_CHAN_L
                  : (idx == DEST_IN2 || idx == DEST_OUT4) ? AI_CHAN_R
                  : AI_CHAN_LR;
    (is_out ? g_shm_out : g_shm_in)->channel_mask = mask;
}

static bool handle_mix_set(const std::string &key, const std::string &val) {
    if (key == "mix.enabled") {
        /* Kept for backward-compat with older control-socket callers; no
         * current GUI surfaces a separate voice on/off (the engine power
         * switch already covers that) - 0 mutes both rings, 1 restores
         * whichever destination was last selected. */
        if (val == "1" || val == "true") apply_dest(g_dest_idx);
        else { g_shm_in->enabled = 0u; g_shm_out->enabled = 0u; }
        return true;
    }
    if (key == "mix.gain") {
        float g = std::strtof(val.c_str(), nullptr) / 100.0f;
        g_shm_in->gain = g;
        g_shm_out->gain = g;
        return true;
    }
    if (key == "mix.dest_idx") {   /* shadow GUI: enum index */
        int idx = std::atoi(val.c_str());
        if (idx < 0 || idx > DEST_OUT34) return false;
        apply_dest(idx);
        return true;
    }
    if (key == "mix.dest") {   /* web GUI: label */
        for (int i = 0; i <= DEST_OUT34; i++) {
            if (val == DEST_LABELS[i]) { apply_dest(i); return true; }
        }
        return false;
    }
    return false;
}
static bool handle_mix_get(const std::string &key, std::string &out) {
    if (key == "mix.enabled") { out = (g_shm_in->enabled || g_shm_out->enabled) ? "1" : "0"; return true; }
    if (key == "mix.gain") { char b[32]; std::snprintf(b, sizeof(b), "%.1f", g_shm_in->gain * 100.0f); out = b; return true; }
    if (key == "mix.dest_idx") { out = std::to_string(g_dest_idx); return true; }
    if (key == "mix.dest") { out = DEST_LABELS[g_dest_idx]; return true; }
    return false;
}

static void handle_ctrl_line(int fd, const std::string &line) {
    char cmd[16] = {0}, key[64] = {0}, val[256] = {0};
    if (sscanf(line.c_str(), "%15s", cmd) != 1) { send(fd, "ERR\n", 4, 0); return; }

    if (!strcmp(cmd, "DESCRIBE")) {
        std::string reply = g_chain_params_json + "\n";
        send(fd, reply.c_str(), reply.size(), 0);
        return;
    }
    if (!strcmp(cmd, "SET") && sscanf(line.c_str(), "%*s %63s %255[^\n]", key, val) == 2) {
        if (handle_mix_set(key, val)) { send(fd, "OK\n", 3, 0); return; }
        std::lock_guard<std::mutex> lk(g_lock);
        g_api->set_param(g_inst, key, val);
        send(fd, "OK\n", 3, 0);
        return;
    }
    if (!strcmp(cmd, "GET") && sscanf(line.c_str(), "%*s %63s", key) == 1) {
        std::string mix_val;
        if (handle_mix_get(key, mix_val)) { std::string reply = mix_val + "\n"; send(fd, reply.c_str(), reply.size(), 0); return; }
        static char buf[65536];
        int n;
        { std::lock_guard<std::mutex> lk(g_lock); n = g_api->get_param(g_inst, key, buf, sizeof(buf)); }
        if (n <= 0) { send(fd, "ERR\n", 4, 0); return; }
        std::string reply(buf, n); reply += "\n";
        send(fd, reply.c_str(), reply.size(), 0);
        return;
    }
    if (!strcmp(cmd, "NOTE")) {
        int note = 60, vel = 100;
        sscanf(line.c_str(), "%*s %d %d", &note, &vel);
        uint8_t on[3]  = { 0x90, (uint8_t)note, (uint8_t)vel };
        uint8_t off[3] = { 0x80, (uint8_t)note, 0 };
        { std::lock_guard<std::mutex> lk(g_lock); g_api->on_midi(g_inst, on, 3, 0); }
        std::thread([off]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            std::lock_guard<std::mutex> lk(g_lock);
            g_api->on_midi(g_inst, off, 3, 0);
        }).detach();
        send(fd, "OK\n", 3, 0);
        return;
    }
    send(fd, "ERR\n", 4, 0);
}

static void ctrl_server_loop(int lfd) {
    while (g_run.load()) {
        int cfd = accept(lfd, nullptr, nullptr);
        if (cfd < 0) continue;
        char buf[512];
        ssize_t n = recv(cfd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = 0;
            std::string line(buf);
            size_t nl = line.find('\n');
            if (nl != std::string::npos) line.resize(nl);
            if (g_verbose) fprintf(stderr, "[dx7] ctrl: %s\n", line.c_str());
            handle_ctrl_line(cfd, line);
        }
        close(cfd);
    }
}

static int ctrl_socket_listen(const std::string &path) {
    unlink(path.c_str());
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) { perror("bind"); close(fd); return -1; }
    chmod(path.c_str(), 0666);
    if (listen(fd, 8) != 0) { perror("listen"); close(fd); return -1; }
    return fd;
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
static void on_signal(int) { g_run.store(false); }

static void usage(const char *me) {
    fprintf(stderr,
        "usage: %s [options]\n"
        "  -v                    verbose\n"
        "  --client NAME         ALSA client name       (default: DX7)\n"
        "  --module-dir PATH     dir containing module.json + banks/ (default: .)\n"
        "  --ctrl-sock PATH      control socket path     (default: /tmp/dx7_ctrl.sock)\n"
        "  --control-channel N   1-16, CC-in for the Q-Link track (default: 1)\n"
        "  --mix-slot N          voice slot 0..%d for forceAudioIn.so (default: 2 -\n"
        "                        0 is force-maze's own default, 1 is force-jv880's)\n",
        me, AI_MAX_VOICES - 1);
}

int main(int argc, char **argv) {
    std::string client = "DX7";
    std::string module_dir = ".";

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if      (a == "-v")                        g_verbose = true;
        else if (a == "--client"     && i+1 < argc) client = argv[++i];
        else if (a == "--module-dir" && i+1 < argc) module_dir = argv[++i];
        else if (a == "--ctrl-sock"  && i+1 < argc) g_ctrl_sock_path = argv[++i];
        else if (a == "--control-channel" && i+1 < argc) g_ctrl_ch = (std::atoi(argv[++i]) - 1) & 0x0F;
        else if (a == "--mix-slot" && i+1 < argc) {
            int s = std::atoi(argv[++i]);
            if (s < 0 || s >= AI_MAX_VOICES) { usage(argv[0]); return 2; }
            g_mix_slot = (unsigned)s;
        }
        else if (a == "--test-tone") { g_test_tone = true; }
        else { usage(argv[0]); return (a == "-h" || a == "--help") ? 0 : 2; }
    }

    for (int i = 0; i < N_PARAMS; i++) g_cc2param[PARAMS[i].cc] = i;

    if (!shm_setup()) { fprintf(stderr, "[dx7] shared memory setup failed\n"); return 1; }

    g_api = move_plugin_init_v2(nullptr);
    if (!g_api || g_api->api_version != 2) { fprintf(stderr, "[dx7] core init failed\n"); return 1; }
    g_inst = g_api->create_instance(module_dir.c_str(), nullptr);
    if (!g_inst) { fprintf(stderr, "[dx7] create_instance failed\n"); return 1; }

    {
        char buf[32768];
        int n = g_api->get_param(g_inst, "chain_params", buf, sizeof(buf));
        g_chain_params_json = (n > 0) ? std::string(buf, n) : std::string("{}");
        if (n <= 0)
            fprintf(stderr, "[dx7] warning: chain_params not found (module.json missing from %s?)\n",
                    module_dir.c_str());
    }

    RtMidiIn *in = nullptr;
    try {
        in = new RtMidiIn(RtMidi::UNSPECIFIED, client, 256);
        in->openVirtualPort("In (Mockba)");
        in->ignoreTypes(true, false, true);   /* keep sysex passthrough off, note+CC/aftertouch on */
        in->setCallback(&on_midi_cb, nullptr);
    } catch (RtMidiError &e) {
        fprintf(stderr, "[dx7] MIDI setup failed: %s\n", e.getMessage().c_str());
        return 1;
    }

    int lfd = ctrl_socket_listen(g_ctrl_sock_path);
    if (lfd < 0) { fprintf(stderr, "[dx7] control socket setup failed\n"); return 1; }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    fprintf(stderr,
        "[dx7] up. port '%s:In (Mockba)'  ctrl socket %s  shm %s  ctrl ch %d\n"
        "[dx7] route a MIDI track to '%s:In (Mockba)' for notes and CC (Q-Link); audio\n"
        "[dx7] is mixed into the Force's capture input via ForceAudioIn (must be enabled).\n",
        client.c_str(), g_ctrl_sock_path.c_str(), g_shm_name_in, g_ctrl_ch + 1, client.c_str());

    std::thread timer(timer_loop);
    std::thread ctrl(ctrl_server_loop, lfd);

    while (g_run.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));

    timer.join();
    close(lfd);
    unlink(g_ctrl_sock_path.c_str());
    {
        std::lock_guard<std::mutex> lk(g_lock);
        g_api->destroy_instance(g_inst);
    }
    delete in;
    if (g_shm_in)  { munmap(g_shm_in, AI_SHM_BYTES); shm_unlink(g_shm_name_in); }
    if (g_shm_out) { munmap(g_shm_out, AI_SHM_BYTES); shm_unlink(g_shm_name_out); }
    fprintf(stderr, "[dx7] bye\n");
    return 0;
}
