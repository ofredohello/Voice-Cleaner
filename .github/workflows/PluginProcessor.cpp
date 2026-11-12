#include "PluginProcessor.h"

VoiceCleanerProcessor::VoiceCleanerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pFeedbackOn  = apvts.getRawParameterValue ("feedbackOn");
    pDenoiseOn   = apvts.getRawParameterValue ("denoiseOn");
    pDenoiseAmt  = apvts.getRawParameterValue ("denoiseAmt");
    pSensitivity = apvts.getRawParameterValue ("sensitivity");
    pMaxCut      = apvts.getRawParameterValue ("maxcut");
    pQ           = apvts.getRawParameterValue ("q");
    pRelease     = apvts.getRawParameterValue ("release");
}

juce::AudioProcessorValueTreeState::ParameterLayout VoiceCleanerProcessor::createLayout()
{
    using F  = juce::AudioParameterFloat;
    using B  = juce::AudioParameterBool;
    using ID = juce::ParameterID;
    using R  = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<B> (ID { "feedbackOn", 1 }, "Anti-larsen", true));
    l.add (std::make_unique<B> (ID { "denoiseOn",   1 }, "Debruitage IA", true));
    l.add (std::make_unique<F> (ID { "denoiseAmt",  1 }, "Force debruitage (%)", R (0.0f, 100.0f, 1.0f), 100.0f));
    l.add (std::make_unique<F> (ID { "sensitivity", 1 }, "Larsen : seuil de detection (dB)", R (6.0f, 30.0f, 0.1f), 15.0f));
    l.add (std::make_unique<F> (ID { "maxcut",      1 }, "Larsen : coupure max (dB)",        R (3.0f, 36.0f, 0.1f), 18.0f));
    l.add (std::make_unique<F> (ID { "q",           1 }, "Larsen : finesse des filtres (Q)", R (5.0f, 80.0f, 0.1f, 0.5f), 30.0f));
    l.add (std::make_unique<F> (ID { "release",     1 }, "Larsen : relachement (dB/s)",      R (0.5f, 20.0f, 0.1f), 3.0f));
    return l;
}

bool VoiceCleanerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void VoiceCleanerProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;

    // --- anti-larsen
    detector.prepare (sr);
    detectedFreqs.reserve (64);
    monoScratch.assign ((size_t) samplesPerBlock, 0.0f);
    slots.fill ({});
    coeffs.fill ({});
    for (auto& ch : states) ch.fill ({});

    // --- debruitage : retard total = reserve de silence + 1 trame RNNoise + marge des reechantillonneurs
    const int frameHost = (int) std::ceil (480.0 * sr / 48000.0);
    const int prefill   = frameHost + 16;
    const bool resampled = std::abs (sr - 48000.0) > 1.0;
    const int latency   = prefill + frameHost + (resampled ? (int) std::lround (2.0 + 2.0 * sr / 48000.0) : 0);

    wetScratch.assign ((size_t) samplesPerBlock, 0.0f);
    for (int c = 0; c < kMaxCh; ++c)
    {
        denoisers[(size_t) c].prepare (sr, samplesPerBlock, prefill);
        dryDelay[(size_t) c].prepare (latency);
    }

    mixSmoothed.reset (sr, 0.05);
    mixSmoothed.setCurrentAndSetTargetValue (0.0f);

    setLatencySamples (latency);   // le logiciel compense automatiquement ce retard
}

VoiceCleanerProcessor::Coeffs VoiceCleanerProcessor::makeNotch (double sr, float f, float q, float gainDb)
{
    // Filtre "peaking" de RBJ avec un gain negatif = creux etroit de profondeur reglable
    const double A     = std::pow (10.0, (double) gainDb / 40.0);
    const double w0    = juce::MathConstants<double>::twoPi * (double) f / sr;
    const double alpha = std::sin (w0) / (2.0 * (double) q);
    const double cw    = std::cos (w0);

    const double b0 = 1.0 + alpha * A, b1 = -2.0 * cw, b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A, a1 = -2.0 * cw, a2 = 1.0 - alpha / A;
    return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
}

void VoiceCleanerProcessor::updateSlots (const std::vector<float>& freqs, double blockSec)
{
    const float maxCut  = pMaxCut->load();
    const float relRate = pRelease->load();
    constexpr double holdTime = 2.0;      // on garde la coupure 2 s apres la derniere detection
    constexpr float  stepDb   = 1.5f;     // on creuse de 1,5 dB a chaque detection

    for (float f : freqs)
    {
        int found = -1, freeIdx = -1, weakest = 0;
        for (int i = 0; i < kSlots; ++i)
        {
            if (slots[(size_t) i].freq <= 0.0f) { if (freeIdx < 0) freeIdx = i; continue; }
            if (std::abs (std::log2 (f / slots[(size_t) i].freq)) < (1.0f / 24.0f)) { found = i; break; }
            if (slots[(size_t) i].targetDb > slots[(size_t) weakest].targetDb) weakest = i;
        }

        if (found >= 0)
        {
            auto& s = slots[(size_t) found];
            s.targetDb = juce::jmax (-maxCut, s.targetDb - stepDb);
            s.holdSec  = holdTime;
        }
        else
        {
            auto& s = slots[(size_t) (freeIdx >= 0 ? freeIdx : weakest)];
            s.freq = f; s.gainDb = 0.0f; s.targetDb = -6.0f; s.holdSec = holdTime;
        }
    }

    const float q = pQ->load();
    for (int i = 0; i < kSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        if (s.freq <= 0.0f) continue;

        if (s.holdSec > 0.0) s.holdSec -= blockSec;
        else                 s.targetDb = juce::jmin (0.0f, s.targetDb + relRate * (float) blockSec);

        s.targetDb = juce::jmax (s.targetDb, -maxCut);
        s.gainDb  += (s.targetDb - s.gainDb) * 0.35f;     // lissage

        if (s.gainDb > -0.3f && s.targetDb >= -0.3f) { s = {}; coeffs[(size_t) i] = {}; continue; }
        coeffs[(size_t) i] = makeNotch (sampleRate, s.freq, q, s.gainDb);
    }
}

void VoiceCleanerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n  = buffer.getNumSamples();
    const int nc = juce::jmin (buffer.getNumChannels(), kMaxCh);

    // ================= 1) Anti-larsen =================
    if (pFeedbackOn->load() > 0.5f)
    {
        if ((int) monoScratch.size() < n) monoScratch.resize ((size_t) n);
        std::fill (monoScratch.begin(), monoScratch.begin() + n, 0.0f);
        for (int c = 0; c < nc; ++c)
            juce::FloatVectorOperations::addWithMultiply (monoScratch.data(), buffer.getReadPointer (c), 1.0f / (float) nc, n);

        detector.setParams (pSensitivity->load(), 5);
        detector.process (monoScratch.data(), n, detectedFreqs);
        updateSlots (detectedFreqs, (double) n / sampleRate);

        for (int c = 0; c < nc; ++c)
        {
            float* x = buffer.getWritePointer (c);
            for (int i = 0; i < kSlots; ++i)
            {
                if (slots[(size_t) i].freq <= 0.0f) { states[(size_t) c][(size_t) i] = {}; continue; }
                const auto& k = coeffs[(size_t) i];
                auto& st = states[(size_t) c][(size_t) i];
                for (int s = 0; s < n; ++s)
                {
                    const float in  = x[s];
                    const float out = k.b0 * in + st.z1;
                    st.z1 = k.b1 * in - k.a1 * out + st.z2;
                    st.z2 = k.b2 * in - k.a2 * out;
                    x[s] = out;
                }
            }
        }
    }
    else
    {
        slots.fill ({});
        coeffs.fill ({});
        for (auto& ch : states) ch.fill ({});
    }

    // ================= 2) Debruitage IA (+ mix son d'origine / son nettoye) =================
    // Le retard est toujours le meme, meme quand le debruitage est coupe : pas de decalage au clic.
    const float amount = (pDenoiseOn->load() > 0.5f) ? pDenoiseAmt->load() * 0.01f : 0.0f;
    mixSmoothed.setTargetValue (amount);

    if ((int) wetScratch.size() < n) wetScratch.resize ((size_t) n);

    for (int c = 0; c < nc; ++c)
    {
        float* x = buffer.getWritePointer (c);
        denoisers[(size_t) c].process (x, wetScratch.data(), n);

        auto smooth = mixSmoothed;          // copie : chaque canal repart du meme point
        for (int i = 0; i < n; ++i)
        {
            const float dry = dryDelay[(size_t) c].process (x[i]);
            const float a   = smooth.getNextValue();
            x[i] = dry + a * (wetScratch[(size_t) i] - dry);
        }
    }
    mixSmoothed.skip (n);
}

void VoiceCleanerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void VoiceCleanerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoiceCleanerProcessor(); }
