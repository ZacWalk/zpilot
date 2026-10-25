// zpilot - procedural audio. Every sound is synthesised at runtime: no wav
// files, no samples. Includes a generative sequencer for the soundtrack.
#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "Common.h"

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace zp {

enum SfxId {
    SFX_SHOT,
    SFX_ENEMY_SHOT,
    SFX_EXPLODE_SMALL,
    SFX_EXPLODE_BIG,
    SFX_PICKUP,
    SFX_LATCH,
    SFX_UNLATCH,
    SFX_HULL_HIT,
    SFX_SHIELD_HIT,
    SFX_REACTOR_HIT,
    SFX_ALARM,
    SFX_TICK,
    SFX_UI,
    SFX_LAUNCH,
    SFX_COUNT
};

class Synth {
public:
    bool Init();
    void Shutdown();

    void Play(SfxId id, float pitch = 1.0f, float gain = 1.0f, float delay = 0.0f);
    void SetThrust(float amount);            // 0..1
    void SetTractor(float amount, float tension);
    void SetMusic(int intensity, uint32_t seed);  // 0 menu, 1 calm, 2 alert, 3 meltdown
    void SetMuted(bool m) { muted_.store(m); }
    bool Muted() const { return muted_.load(); }

private:
    struct Voice {
        bool active = false;
        int wave = 0;  // 0 saw, 1 pulse, 2 tri, 3 noise, 4 sine, 5 fm
        float phase = 0, phase2 = 0;
        float f0 = 440, f1 = 440, fTime = 0.1f;
        float t = 0, dur = 0.3f;
        float attack = 0.004f, decay = 0.2f, sustain = 0.0f, release = 0.08f;
        float gain = 0.25f, pw = 0.5f;
        float cut0 = 1.0f, cut1 = 0.25f, res = 0.15f;
        float lp1 = 0, lp2 = 0, bp = 0;
        float vibRate = 0, vibDepth = 0;
        float fmRatio = 2.0f, fmDepth = 0.0f;
        float pan = 0.5f, send = 0.25f;
        bool pluck = false;  // decays on a curve: front-loaded, like a plucked string
        bool duck = false;   // on the music bus, so the kick can push it aside
        uint32_t rngState = 1;
    };

    struct Pending {
        SfxId id;
        float pitch, gain, delay;
    };

    void RenderBlock(float* out, int frames);
    void RenderVoice(Voice& v, float& l, float& r, float& send);
    Voice* Alloc();
    void Trigger(SfxId id, float pitch, float gain);
    void StepSequencer();
    static void ThreadEntry(Synth* s);
    void ThreadLoop();

    IXAudio2* xaudio_ = nullptr;
    IXAudio2MasteringVoice* master_ = nullptr;
    IXAudio2SourceVoice* voice_ = nullptr;
    void* bufferEvent_ = nullptr;
    struct CallbackImpl;
    CallbackImpl* callback_ = nullptr;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> muted_{false};
    std::atomic<float> thrust_{0}, tractor_{0}, tension_{0};
    std::atomic<int> intensity_{1};
    std::atomic<uint32_t> musicSeed_{1};

    std::mutex queueLock_;
    std::vector<Pending> queue_;
    std::vector<Pending> scheduled_;

    static const int kSampleRate = 44100;
    static const int kBlockFrames = 512;
    static const int kBlocks = 4;
    static const int kMaxVoices = 40;
    static const int kDelaySamples = 22050;

    std::vector<Voice> voices_;
    std::vector<float> blocks_[kBlocks];
    int blockIndex_ = 0;

    // Continuous engine / tractor voices live outside the pool.
    float thrustPhase_ = 0, thrustLp_ = 0, thrustLevel_ = 0, rumblePhase_ = 0;
    float tractorPhase_ = 0, tractorLevel_ = 0, tractorVib_ = 0;
    uint32_t noiseState_ = 22222;

    std::vector<float> delayL_, delayR_;
    int delayPos_ = 0;
    float duck_ = 0, duckGain_ = 1;

    double stepAccum_ = 0;
    int step_ = 0;
    uint32_t lastSeed_ = 0;
    // 23 is coprime with the 16 step bar, so the melody walks around the beat
    // and only lines up again 23 bars later. Entries are steps of the minor
    // scale above the tonic, so the line can be moved onto any chord in key.
    static const int kMotifLen = 23;
    int motifA_[kMotifLen] = {};
    int motifB_[kMotifLen] = {};
    int songTranspose_ = 0;
    Rng musicRng_{1};
};

}  // namespace zp
