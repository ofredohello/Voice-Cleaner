#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "FeedbackDetector.h"
#include "Denoiser.h"
#include <array>
#include <vector>

class VoiceCleanerProcessor : public juce::AudioProcessor
{
public:
    VoiceCleanerProcessor();
    ~VoiceCleanerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Voice Cleaner"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- Anti-larsen
    static constexpr int kSlots = 12;
    static constexpr int kMaxCh = 2;

    struct Slot   { float freq = 0.0f; float gainDb = 0.0f; float targetDb = 0.0f; double holdSec = 0.0; };
    struct Coeffs { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State  { float z1 = 0, z2 = 0; };

    void updateSlots (const std::vector<float>& freqs, double blockSec);
    static Coeffs makeNotch (double sr, float freq, float q, float gainDb);

    std::array<Slot, kSlots> slots;
    std::array<Coeffs, kSlots> coeffs;
    std::array<std::array<State, kSlots>, kMaxCh> states;

    FeedbackDetector detector;
    std::vector<float> detectedFreqs;
    std::vector<float> monoScratch;

    // ---- Debruitage IA
    std::array<DenoiseChannel, kMaxCh> denoisers;
    std::array<DelayLine, kMaxCh> dryDelay;
    std::vector<float> wetScratch;
    juce::SmoothedValue<float> mixSmoothed;

    double sampleRate = 44100.0;

    std::atomic<float>* pFeedbackOn  = nullptr;
    std::atomic<float>* pDenoiseOn   = nullptr;
    std::atomic<float>* pDenoiseAmt  = nullptr;   // 0..100 %
    std::atomic<float>* pSensitivity = nullptr;   // seuil de detection (dB)
    std::atomic<float>* pMaxCut      = nullptr;   // coupure max (dB)
    std::atomic<float>* pQ           = nullptr;
    std::atomic<float>* pRelease     = nullptr;   // dB/s

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceCleanerProcessor)
};
