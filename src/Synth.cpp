#include "Synth.h"

#include <xaudio2.h>

#pragma comment(lib, "xaudio2.lib")

namespace zp {

struct Synth::CallbackImpl : public IXAudio2VoiceCallback {
    HANDLE ev = nullptr;
    void __stdcall OnVoiceProcessingPassStart(UINT32) override {}
    void __stdcall OnVoiceProcessingPassEnd() override {}
    void __stdcall OnStreamEnd() override {}
    void __stdcall OnBufferStart(void*) override {}
    void __stdcall OnBufferEnd(void*) override {
        if (ev) SetEvent(ev);
    }
    void __stdcall OnLoopEnd(void*) override {}
    void __stdcall OnVoiceError(void*, HRESULT) override {}
};

static inline float noiseFrom(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return ((s >> 8) * (1.0f / 8388608.0f)) - 1.0f;
}

static inline float midiToHz(float n) { return 440.0f * powf(2.0f, (n - 69.0f) / 12.0f); }

bool Synth::Init() {
    voices_.resize(kMaxVoices);
    for (int i = 0; i < kBlocks; ++i) blocks_[i].assign(kBlockFrames * 2, 0.0f);
    delayL_.assign(kDelaySamples, 0.0f);
    delayR_.assign(kDelaySamples, 0.0f);

    if (FAILED(XAudio2Create(&xaudio_, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
    if (FAILED(xaudio_->CreateMasteringVoice(&master_))) return false;

    WAVEFORMATEX wf = {};
    wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wf.nChannels = 2;
    wf.nSamplesPerSec = kSampleRate;
    wf.wBitsPerSample = 32;
    wf.nBlockAlign = (wf.wBitsPerSample / 8) * wf.nChannels;
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    bufferEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    callback_ = new CallbackImpl();
    callback_->ev = (HANDLE)bufferEvent_;

    if (FAILED(xaudio_->CreateSourceVoice(&voice_, &wf, 0, XAUDIO2_DEFAULT_FREQ_RATIO, callback_)))
        return false;
    voice_->SetVolume(1.0f);
    voice_->Start(0);

    running_.store(true);
    thread_ = std::thread(&Synth::ThreadEntry, this);
    return true;
}

void Synth::Shutdown() {
    if (running_.exchange(false)) {
        if (bufferEvent_) SetEvent((HANDLE)bufferEvent_);
        if (thread_.joinable()) thread_.join();
    }
    if (voice_) {
        voice_->Stop(0);
        voice_->DestroyVoice();
        voice_ = nullptr;
    }
    if (master_) {
        master_->DestroyVoice();
        master_ = nullptr;
    }
    if (xaudio_) {
        xaudio_->Release();
        xaudio_ = nullptr;
    }
    if (bufferEvent_) {
        CloseHandle((HANDLE)bufferEvent_);
        bufferEvent_ = nullptr;
    }
    delete callback_;
    callback_ = nullptr;
}

void Synth::ThreadEntry(Synth* s) { s->ThreadLoop(); }

void Synth::ThreadLoop() {
    while (running_.load()) {
        XAUDIO2_VOICE_STATE state = {};
        voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        while (state.BuffersQueued < (UINT32)kBlocks && running_.load()) {
            std::vector<float>& blk = blocks_[blockIndex_];
            RenderBlock(blk.data(), kBlockFrames);

            XAUDIO2_BUFFER buf = {};
            buf.AudioBytes = (UINT32)(blk.size() * sizeof(float));
            buf.pAudioData = reinterpret_cast<const BYTE*>(blk.data());
            voice_->SubmitSourceBuffer(&buf);

            blockIndex_ = (blockIndex_ + 1) % kBlocks;
            voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        }
        WaitForSingleObject((HANDLE)bufferEvent_, 20);
    }
}

void Synth::Play(SfxId id, float pitch, float gain, float delay) {
    std::lock_guard<std::mutex> lock(queueLock_);
    if (queue_.size() < 128) queue_.push_back({id, pitch, gain, delay});
}

void Synth::SetThrust(float amount) { thrust_.store(saturatef(amount)); }

void Synth::SetTractor(float amount, float tension) {
    tractor_.store(saturatef(amount));
    tension_.store(saturatef(tension));
}

void Synth::SetMusic(int intensity, uint32_t seed) {
    intensity_.store(intensity);
    musicSeed_.store(seed ? seed : 1u);
}

Synth::Voice* Synth::Alloc() {
    Voice* oldest = nullptr;
    for (auto& v : voices_) {
        if (!v.active) {
            v = Voice();
            v.rngState = (uint32_t)(&v - voices_.data()) * 2654435761u + 17u;
            return &v;
        }
        if (!oldest || v.t > oldest->t) oldest = &v;
    }
    if (oldest) {
        uint32_t keep = oldest->rngState;
        *oldest = Voice();
        oldest->rngState = keep ? keep : 7u;
    }
    return oldest;
}

void Synth::Trigger(SfxId id, float pitch, float gain) {
    Voice* v = Alloc();
    if (!v) return;
    v->active = true;

    switch (id) {
        case SFX_SHOT:
            v->wave = 5;
            v->f0 = 1500 * pitch; v->f1 = 260 * pitch; v->fTime = 0.08f;
            v->fmRatio = 1.51f; v->fmDepth = 2.2f;
            v->attack = 0.001f; v->decay = 0.10f; v->dur = 0.10f; v->release = 0.05f;
            v->cut0 = 1.0f; v->cut1 = 0.45f; v->gain = 0.20f * gain; v->send = 0.30f;
            break;
        case SFX_ENEMY_SHOT:
            v->wave = 1; v->pw = 0.28f;
            v->f0 = 760 * pitch; v->f1 = 150 * pitch; v->fTime = 0.10f;
            v->attack = 0.002f; v->decay = 0.13f; v->dur = 0.13f; v->release = 0.06f;
            v->cut0 = 0.85f; v->cut1 = 0.30f; v->gain = 0.15f * gain; v->send = 0.35f;
            break;
        case SFX_EXPLODE_SMALL:
            v->wave = 3;
            v->attack = 0.001f; v->decay = 0.38f; v->dur = 0.38f; v->release = 0.20f;
            v->cut0 = 0.85f; v->cut1 = 0.06f; v->res = 0.35f;
            v->gain = 0.32f * gain; v->send = 0.45f;
            break;
        case SFX_EXPLODE_BIG: {
            v->wave = 3;
            v->attack = 0.001f; v->decay = 1.25f; v->dur = 1.25f; v->release = 0.6f;
            v->cut0 = 0.75f; v->cut1 = 0.015f; v->res = 0.42f;
            v->gain = 0.45f * gain; v->send = 0.6f;
            Voice* b = Alloc();
            if (b) {
                b->active = true;
                b->wave = 4;
                b->f0 = 110 * pitch; b->f1 = 26 * pitch; b->fTime = 0.9f;
                b->attack = 0.002f; b->decay = 1.1f; b->dur = 1.1f; b->release = 0.4f;
                b->cut0 = 0.6f; b->cut1 = 0.2f; b->gain = 0.5f * gain; b->send = 0.3f;
            }
            break;
        }
        case SFX_PICKUP:
            v->wave = 4;
            v->f0 = 620 * pitch; v->f1 = 1560 * pitch; v->fTime = 0.13f;
            v->attack = 0.004f; v->decay = 0.22f; v->dur = 0.22f; v->release = 0.15f;
            v->cut0 = 1.0f; v->cut1 = 0.8f; v->gain = 0.18f * gain; v->send = 0.5f;
            break;
        case SFX_LATCH:
            v->wave = 1; v->pw = 0.4f;
            v->f0 = 140 * pitch; v->f1 = 420 * pitch; v->fTime = 0.12f;
            v->attack = 0.003f; v->decay = 0.18f; v->dur = 0.18f; v->release = 0.1f;
            v->cut0 = 0.7f; v->cut1 = 0.5f; v->gain = 0.20f * gain; v->send = 0.4f;
            break;
        case SFX_UNLATCH:
            v->wave = 1; v->pw = 0.4f;
            v->f0 = 420 * pitch; v->f1 = 130 * pitch; v->fTime = 0.12f;
            v->attack = 0.003f; v->decay = 0.16f; v->dur = 0.16f; v->release = 0.1f;
            v->cut0 = 0.7f; v->cut1 = 0.4f; v->gain = 0.18f * gain; v->send = 0.4f;
            break;
        case SFX_HULL_HIT:
            v->wave = 3;
            v->attack = 0.001f; v->decay = 0.22f; v->dur = 0.22f; v->release = 0.15f;
            v->cut0 = 0.55f; v->cut1 = 0.08f; v->res = 0.45f;
            v->gain = 0.36f * gain; v->send = 0.4f;
            break;
        case SFX_SHIELD_HIT:
            v->wave = 4;
            v->f0 = 1500 * pitch; v->f1 = 520 * pitch; v->fTime = 0.22f;
            v->vibRate = 24.0f; v->vibDepth = 0.05f;
            v->attack = 0.002f; v->decay = 0.30f; v->dur = 0.30f; v->release = 0.2f;
            v->cut0 = 1.0f; v->cut1 = 0.6f; v->gain = 0.22f * gain; v->send = 0.6f;
            break;
        case SFX_REACTOR_HIT:
            v->wave = 5;
            v->f0 = 340 * pitch; v->f1 = 70 * pitch; v->fTime = 0.3f;
            v->fmRatio = 2.51f; v->fmDepth = 4.5f;
            v->attack = 0.002f; v->decay = 0.42f; v->dur = 0.42f; v->release = 0.25f;
            v->cut0 = 0.8f; v->cut1 = 0.15f; v->gain = 0.34f * gain; v->send = 0.5f;
            break;
        case SFX_ALARM:
            v->wave = 1; v->pw = 0.5f;
            v->f0 = 980 * pitch; v->f1 = 660 * pitch; v->fTime = 0.3f;
            v->attack = 0.01f; v->decay = 0.05f; v->sustain = 0.7f; v->dur = 0.34f;
            v->release = 0.12f;
            v->cut0 = 0.8f; v->cut1 = 0.5f; v->gain = 0.20f * gain; v->send = 0.5f;
            break;
        case SFX_TICK:
            v->wave = 1; v->pw = 0.2f;
            v->f0 = 1750 * pitch; v->f1 = 1500 * pitch; v->fTime = 0.05f;
            v->attack = 0.001f; v->decay = 0.06f; v->dur = 0.06f; v->release = 0.04f;
            v->cut0 = 1.0f; v->cut1 = 0.9f; v->gain = 0.14f * gain; v->send = 0.3f;
            break;
        case SFX_UI:
            v->wave = 2;
            v->f0 = 520 * pitch; v->f1 = 990 * pitch; v->fTime = 0.05f;
            v->attack = 0.002f; v->decay = 0.10f; v->dur = 0.10f; v->release = 0.06f;
            v->cut0 = 1.0f; v->cut1 = 0.8f; v->gain = 0.16f * gain; v->send = 0.4f;
            break;
        case SFX_LAUNCH:
            v->wave = 0;
            v->f0 = 90 * pitch; v->f1 = 1200 * pitch; v->fTime = 1.1f;
            v->attack = 0.05f; v->decay = 0.2f; v->sustain = 0.6f; v->dur = 1.1f;
            v->release = 0.4f;
            v->cut0 = 0.4f; v->cut1 = 1.0f; v->gain = 0.24f * gain; v->send = 0.6f;
            break;
        default:
            v->active = false;
            break;
    }
}

void Synth::RenderVoice(Voice& v, float& outL, float& outR, float& outSend) {
    const float sr = (float)kSampleRate;

    float e;
    float ad = v.attack + v.decay;
    if (v.t < v.attack)
        e = v.attack > 0 ? v.t / v.attack : 1.0f;
    else if (v.t < ad)
        e = lerpf(1.0f, v.sustain, (v.t - v.attack) / std::max(v.decay, 1e-5f));
    else if (v.t < v.dur)
        e = v.sustain;
    else
        e = v.sustain * saturatef(1.0f - (v.t - v.dur) / std::max(v.release, 1e-5f));
    if (v.t > std::max(v.dur, ad) + v.release) {
        v.active = false;
        return;
    }

    float u = saturatef(v.fTime > 0 ? v.t / v.fTime : 1.0f);
    float freq = v.f0 * powf(std::max(v.f1, 1.0f) / std::max(v.f0, 1.0f), u);
    if (v.vibDepth > 0) freq *= 1.0f + sinf(kTau * v.vibRate * v.t) * v.vibDepth;

    v.phase += freq / sr;
    if (v.phase >= 1.0f) v.phase -= floorf(v.phase);
    v.phase2 += freq * v.fmRatio / sr;
    if (v.phase2 >= 1.0f) v.phase2 -= floorf(v.phase2);

    float o = 0;
    switch (v.wave) {
        case 0: o = v.phase * 2.0f - 1.0f; break;
        case 1: o = v.phase < v.pw ? 1.0f : -1.0f; break;
        case 2: o = 4.0f * fabsf(v.phase - 0.5f) - 1.0f; break;
        case 3: o = noiseFrom(v.rngState); break;
        case 4: o = sinf(kTau * v.phase); break;
        default: o = sinf(kTau * v.phase + v.fmDepth * sinf(kTau * v.phase2)); break;
    }

    float total = std::max(v.dur, ad) + v.release;
    float cutN = lerpf(v.cut0, v.cut1, saturatef(v.t / std::max(total, 1e-4f)));
    float fc = 60.0f * powf(300.0f, saturatef(cutN));
    float a = 1.0f - expf(-kTau * fc / sr);
    a = clampf(a, 0.0005f, 0.98f);

    float x = o - clampf(v.res, 0.0f, 0.48f) * 1.6f * v.lp2;
    v.lp1 += a * (x - v.lp1);
    v.lp2 += a * (v.lp1 - v.lp2);

    float s = v.lp2 * e * v.gain;
    outL += s * (1.0f - v.pan);
    outR += s * v.pan;
    outSend += s * v.send;

    v.t += 1.0f / sr;
}

// ------------------------------------------------------------------- music
namespace {

// Chords are a bass root plus a four note voicing. A natural minor palette with
// enough colour tones that the arpeggio has somewhere to go.
struct Chord {
    int8_t root;
    int8_t iv[4];
};
static const Chord kChords[8] = {
    {45, {0, 3, 7, 12}},  // 0  i        A minor
    {41, {0, 4, 7, 11}},  // 1  VI maj7  F
    {48, {0, 4, 7, 10}},  // 2  III7     C
    {43, {0, 4, 7, 12}},  // 3  VII      G
    {50, {0, 3, 7, 10}},  // 4  iv7      D minor
    {40, {0, 5, 7, 12}},  // 5  Vsus     E sus4
    {47, {0, 3, 6, 10}},  // 6  ii-      B half diminished
    {45, {0, 3, 7, 14}},  // 7  i add9
};

// One tracker row per bar. Sixteen bars of order list, and the textures change
// underneath the harmony, so the ear hears a tune rather than a two bar loop.
struct SongRow {
    uint8_t chord, bass, arp, drum, lead;
};
static const SongRow kSong[16] = {
    {0, 0, 1, 0, 0},  // A - intro, bass and pad only
    {0, 0, 1, 1, 0},
    {1, 1, 1, 1, 1},
    {3, 1, 2, 1, 1},
    {4, 0, 1, 2, 0},  // B - lift
    {2, 2, 2, 1, 2},
    {1, 1, 2, 2, 1},
    {5, 2, 3, 3, 1},
    {0, 1, 2, 1, 2},  // C - drive
    {6, 1, 2, 1, 0},
    {3, 2, 2, 2, 1},
    {2, 2, 3, 2, 3},
    {4, 1, 2, 1, 1},  // D - turnaround
    {1, 2, 2, 2, 2},
    {5, 2, 3, 2, 3},
    {7, 3, 1, 3, 0},
};

static const uint8_t kBassPat[4][16] = {
    {1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0},
    {1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1},
    {1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1},
    {1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1},
};
static const uint8_t kKickPat[4][16] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    {1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0},
    {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1},
};
static const uint8_t kSnarePat[4][16] = {
    {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1},
    {0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1},
};
static const uint8_t kHatPat[4][16] = {
    {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0},
    {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};

}  // namespace

void Synth::StepSequencer() {
    const int intensity = intensity_.load();
    const uint32_t seed = musicSeed_.load();
    if (seed != lastSeed_) {
        lastSeed_ = seed;
        // Two motifs per planet. Regenerated only when the planet changes, so
        // the tune stays the tune while the arrangement moves around it.
        Rng r(seed * 2654435761u + 7u);
        static const int scale[7] = {0, 2, 3, 5, 7, 8, 10};
        for (int i = 0; i < kMotifLen; ++i) {
            motifA_[i] = r.f01() < 0.30f ? -1 : scale[r.rangeI(0, 6)] + (r.f01() < 0.18f ? 12 : 0);
            motifB_[i] = r.f01() < 0.42f ? -1 : scale[r.rangeI(0, 6)];
        }
        motifA_[0] = 0;
        songTranspose_ = (int)(seed % 5u) - 2;
    }

    const int bar = (step_ / 16) & 15;
    const int s = step_ % 16;
    SongRow row = kSong[bar];

    // Intensity re-scores the row rather than switching to a different tune, so
    // the transition into an alert never sounds like a needle lift.
    if (intensity <= 1) {
        if (row.drum > 1) row.drum = 1;
        if (row.arp > 1) row.arp = 1;
        if (row.lead > 1) row.lead = 1;
    } else if (intensity >= 3) {
        row.drum = 3;
        if (row.arp < 2) row.arp = 2;
    }

    const Chord& ch = kChords[row.chord];
    const int shift = songTranspose_ + (intensity >= 3 ? 6 : 0);  // meltdown: tritone, all wrong
    const int root = ch.root + shift;

    auto note = [&](int wave, float midi, float gain, float dur, float pw, float pan, float cut0,
                    float cut1, float send) {
        Voice* v = Alloc();
        if (!v) return;
        v->active = true;
        v->wave = wave;
        v->f0 = v->f1 = midiToHz(midi);
        v->fTime = 0.01f;
        v->pw = pw;
        v->attack = 0.004f;
        v->decay = dur;
        v->dur = dur;
        v->release = dur * 0.5f;
        v->gain = gain;
        v->cut0 = cut0;
        v->cut1 = cut1;
        v->res = 0.25f;
        v->pan = pan;
        v->send = send;
    };

    auto perc = [&](float decay, float cut0, float cut1, float res, float gain, float pan,
                    float send) {
        Voice* v = Alloc();
        if (!v) return;
        v->active = true;
        v->wave = 3;
        v->attack = 0.0006f;
        v->decay = decay;
        v->dur = decay;
        v->release = decay * 0.5f;
        v->cut0 = cut0;
        v->cut1 = cut1;
        v->res = res;
        v->gain = gain;
        v->pan = pan;
        v->send = send;
    };

    if (intensity <= 0) {
        // Menu: the same harmony, breathing.
        if (s == 0)
            for (int i = 0; i < 4; ++i)
                note(0, (float)(root + ch.iv[i] + 12), 0.040f, 2.2f, 0.5f, 0.25f + i * 0.17f, 0.30f,
                     0.14f, 0.75f);
        if (s == 9)
            note(2, (float)(root + ch.iv[(bar + 2) & 3] + 36), 0.030f, 0.9f, 0.25f,
                 (bar & 1) ? 0.75f : 0.25f, 0.9f, 0.35f, 0.85f);
        ++step_;
        return;
    }

    // Bass: root on the downbeats, fifth and octave off them, so the line walks.
    if (kBassPat[row.bass][s]) {
        int off = (s == 0 || s == 8) ? 0 : (s >= 12 ? 7 : ((s & 3) == 2 ? 12 : 0));
        note(0, (float)(root + off - 12), 0.20f, (s % 4) == 0 ? 0.20f : 0.13f, 0.5f, 0.5f, 0.44f,
             0.12f, 0.12f);
    }

    // Arpeggio: eighths, sixteenths or a broken figure with octave jumps.
    if (row.arp) {
        bool hit = row.arp == 1 ? (s % 2) == 0 : (row.arp == 2 ? true : (s % 4) != 3);
        if (hit) {
            int seq = step_ / (row.arp == 1 ? 2 : 1);
            int idx = (bar & 2) ? 3 - (seq & 3) : (seq & 3);  // the figure inverts every 2 bars
            int oct = (row.arp == 3 && (seq & 4)) ? 12 : 0;
            note(1, (float)(root + ch.iv[idx] + 24 + oct), intensity >= 2 ? 0.070f : 0.052f, 0.12f,
                 0.30f, (s & 1) ? 0.74f : 0.26f, 0.95f, 0.55f, 0.55f);
        }
    }

    // Lead: the planet's motif, transposed onto whatever chord is underneath.
    if (row.lead && (s % 2) == 0) {
        const int* motif = row.lead == 2 ? motifB_ : motifA_;
        int n = motif[(step_ / 2) % kMotifLen];
        if (n >= 0) {
            float m = (float)(root + n + 24 + (row.lead == 3 ? 12 : 0));
            note(1, m, 0.070f, 0.28f, 0.18f, 0.5f, 1.0f, 0.45f, 0.78f);
            if (row.lead == 3) note(2, m - 5.0f, 0.038f, 0.24f, 0.5f, 0.34f, 0.9f, 0.40f, 0.80f);
            if (musicRng_.f01() < 0.10f)
                note(1, m + 12.0f, 0.028f, 0.06f, 0.20f, 0.66f, 1.0f, 0.70f, 0.60f);
        }
    } else if (!row.lead && s == 0) {
        // No melody this bar: hold a pad so the gap is a rest, not a hole.
        for (int i = 1; i < 4; ++i)
            note(2, (float)(root + ch.iv[i] + 12), 0.030f, 1.5f, 0.5f, 0.30f + i * 0.18f, 0.35f,
                 0.18f, 0.70f);
    }

    if (kKickPat[row.drum][s]) {
        Voice* v = Alloc();
        if (v) {
            v->active = true;
            v->wave = 4;
            v->f0 = 150; v->f1 = 42; v->fTime = 0.06f;
            v->attack = 0.001f; v->decay = 0.22f; v->dur = 0.22f; v->release = 0.05f;
            v->cut0 = 0.6f; v->cut1 = 0.3f; v->gain = 0.34f; v->send = 0.05f;
        }
    }
    if (kSnarePat[row.drum][s]) perc(0.16f, 0.72f, 0.35f, 0.30f, 0.17f, 0.5f, 0.45f);
    if (kHatPat[row.drum][s])
        perc(0.035f, 1.0f, 0.95f, 0.0f, (s & 1) ? 0.035f : 0.052f, (s & 1) ? 0.40f : 0.62f, 0.30f);

    // Crash on each four bar section boundary.
    if (s == 0 && (bar & 3) == 0) perc(0.9f, 0.98f, 0.55f, 0.05f, 0.10f, 0.5f, 0.7f);

    // Meltdown: a drone under everything.
    if (intensity >= 3 && (s % 8) == 0)
        note(0, (float)(root - 12), 0.085f, 0.9f, 0.5f, 0.5f, 0.30f, 0.10f, 0.60f);

    ++step_;
}

void Synth::RenderBlock(float* out, int frames) {
    const float sr = (float)kSampleRate;
    const float blockSeconds = frames / sr;

    {
        std::lock_guard<std::mutex> lock(queueLock_);
        for (auto& p : queue_) {
            if (p.delay <= 0.0f)
                Trigger(p.id, p.pitch, p.gain);
            else
                scheduled_.push_back(p);
        }
        queue_.clear();
    }
    for (size_t i = 0; i < scheduled_.size();) {
        scheduled_[i].delay -= blockSeconds;
        if (scheduled_[i].delay <= 0.0f) {
            Trigger(scheduled_[i].id, scheduled_[i].pitch, scheduled_[i].gain);
            scheduled_[i] = scheduled_.back();
            scheduled_.pop_back();
        } else {
            ++i;
        }
    }

    int intensity = intensity_.load();
    float bpm = 92.0f + intensity * 4.0f + (intensity >= 3 ? 26.0f : 0.0f);
    double samplesPerStep = sr * 60.0 / bpm / 4.0;

    const float muteGain = muted_.load() ? 0.0f : 1.0f;
    const float thrustTarget = thrust_.load();
    const float tractorTarget = tractor_.load();
    const float tensionAmt = tension_.load();

    for (int i = 0; i < frames; ++i) {
        stepAccum_ += 1.0;
        if (stepAccum_ >= samplesPerStep) {
            stepAccum_ -= samplesPerStep;
            StepSequencer();
        }

        float l = 0, r = 0, send = 0;
        for (auto& v : voices_)
            if (v.active) RenderVoice(v, l, r, send);

        // Continuous engine roar: filtered noise + a sub rumble.
        thrustLevel_ += (thrustTarget - thrustLevel_) * 0.0016f;
        if (thrustLevel_ > 0.0005f) {
            float n = noiseFrom(noiseState_);
            float fc = 260.0f + 3200.0f * thrustLevel_;
            float a = clampf(1.0f - expf(-kTau * fc / sr), 0.001f, 0.9f);
            thrustLp_ += a * (n - thrustLp_);
            rumblePhase_ += (46.0f + 34.0f * thrustLevel_) / sr;
            if (rumblePhase_ >= 1.0f) rumblePhase_ -= 1.0f;
            thrustPhase_ += (150.0f + 90.0f * thrustLevel_) / sr;
            if (thrustPhase_ >= 1.0f) thrustPhase_ -= 1.0f;
            float body = thrustLp_ * 0.85f + sinf(kTau * rumblePhase_) * 0.55f +
                         (thrustPhase_ * 2.0f - 1.0f) * 0.12f;
            float s = body * thrustLevel_ * 0.115f;
            l += s;
            r += s;
            send += s * 0.25f;
        }

        // Tractor beam hum, pitched by tether tension.
        tractorLevel_ += (tractorTarget - tractorLevel_) * 0.0018f;
        if (tractorLevel_ > 0.0005f) {
            float f = 132.0f + tensionAmt * 300.0f;
            tractorPhase_ += f / sr;
            if (tractorPhase_ >= 1.0f) tractorPhase_ -= 1.0f;
            tractorVib_ += 5.5f / sr;
            if (tractorVib_ >= 1.0f) tractorVib_ -= 1.0f;
            float s = (sinf(kTau * tractorPhase_) * 0.7f +
                       sinf(kTau * tractorPhase_ * 3.01f) * 0.25f) *
                      (0.8f + 0.2f * sinf(kTau * tractorVib_)) * tractorLevel_ * 0.14f;
            l += s;
            r += s;
            send += s * 0.5f;
        }

        // Stereo ping-pong delay for a sense of cavern space.
        int tapL = delayPos_ - 13230;
        int tapR = delayPos_ - 19845;
        if (tapL < 0) tapL += kDelaySamples;
        if (tapR < 0) tapR += kDelaySamples;
        float dl = delayL_[tapL];
        float dr = delayR_[tapR];
        delayL_[delayPos_] = send * 0.5f + dr * 0.42f;
        delayR_[delayPos_] = send * 0.5f + dl * 0.42f;
        delayPos_ = (delayPos_ + 1) % kDelaySamples;

        l = (l + dl * 0.55f) * 0.9f;
        r = (r + dr * 0.55f) * 0.9f;

        l = tanhf(l * 1.25f) * 0.82f * muteGain;
        r = tanhf(r * 1.25f) * 0.82f * muteGain;

        out[i * 2 + 0] = l;
        out[i * 2 + 1] = r;
    }

    sampleClock_ += frames;
}

}  // namespace zp
