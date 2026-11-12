#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>

// Detecte les fréquences "larsen" : pics spectraux étroits, saillants par rapport
// à leur voisinage, et qui persistent sur plusieurs trames consécutives.
class FeedbackDetector
{
public:
    static constexpr int order = 12;
    static constexpr int N     = 1 << order;   // 4096
    static constexpr int hop   = 1024;
    static constexpr int bins  = N / 2;

    FeedbackDetector() : fft (order), window (N, juce::dsp::WindowingFunction<float>::hann)
    {
        reset();
        detected.reserve (16);
    }

    void prepare (double sr) { sampleRate = sr; reset(); }

    void reset()
    {
        ring.fill (0.0f);
        persist.fill (0);
        writePos = 0;
        sinceLastFrame = 0;
    }

    void setParams (float prominenceDb, int persistFrames)
    {
        thresholdDb = prominenceDb;
        minPersist  = persistFrames;
    }

    // Pousse du signal mono. Remplit `out` avec les fréquences (Hz) détectées comme larsen.
    void process (const float* mono, int n, std::vector<float>& out)
    {
        out.clear();
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) writePos] = mono[i];
            writePos = (writePos + 1) % N;
            if (++sinceLastFrame >= hop)
            {
                sinceLastFrame = 0;
                analyseFrame (out);
            }
        }
    }

private:
    void analyseFrame (std::vector<float>& out)
    {
        // Copie ordonnée des N derniers échantillons
        for (int i = 0; i < N; ++i)
            fftData[(size_t) i] = ring[(size_t) ((writePos + i) % N)];
        std::fill (fftData.begin() + N, fftData.end(), 0.0f);

        window.multiplyWithWindowingTable (fftData.data(), N);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        // Magnitude -> dB (sinus d'amplitude A avec Hann => pic ≈ A * N / 4)
        const float norm = 4.0f / (float) N;
        for (int k = 0; k < bins; ++k)
            db[(size_t) k] = juce::Decibels::gainToDecibels (fftData[(size_t) k] * norm, -120.0f);

        const float binHz  = (float) sampleRate / (float) N;
        const int   kMin   = juce::jmax (9, (int) std::ceil (80.0f / binHz));
        const int   kMax   = juce::jmin (bins - 10, (int) (sampleRate * 0.45 / binHz));

        std::array<char, bins> isPeak {};
        for (int k = kMin; k < kMax; ++k)
        {
            const float v = db[(size_t) k];
            if (v < -65.0f || !(v > db[(size_t) k - 1] && v >= db[(size_t) k + 1]))
                continue;

            // Moyenne du voisinage (en excluant le lobe principal ±3 bins)
            float sum = 0.0f;
            for (int j = 4; j <= 8; ++j)
                sum += db[(size_t) (k - j)] + db[(size_t) (k + j)];
            const float neighbourAvg = sum / 10.0f;

            if (v - neighbourAvg > thresholdDb)
                isPeak[(size_t) k] = 1;
        }

        // Persistance : un pic compte comme le même s'il bouge de ±1 bin
        std::array<int, bins> newPersist {};
        for (int k = kMin; k < kMax; ++k)
        {
            if (! isPeak[(size_t) k]) continue;
            const int prev = juce::jmax (persist[(size_t) k - 1],
                             juce::jmax (persist[(size_t) k], persist[(size_t) k + 1]));
            newPersist[(size_t) k] = prev + 1;

            if (newPersist[(size_t) k] >= minPersist)
            {
                // Interpolation parabolique pour affiner la fréquence
                const float a = db[(size_t) k - 1], b = db[(size_t) k], c = db[(size_t) k + 1];
                const float denom = a - 2.0f * b + c;
                const float delta = std::abs (denom) > 1e-6f ? 0.5f * (a - c) / denom : 0.0f;
                out.push_back (((float) k + juce::jlimit (-0.5f, 0.5f, delta)) * binHz);
            }
        }
        persist = newPersist;
    }

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, N> ring {};
    std::array<float, 2 * N> fftData {};
    std::array<float, bins> db {};
    std::array<int, bins> persist {};
    std::vector<float> detected;
    int writePos = 0, sinceLastFrame = 0;
    double sampleRate = 44100.0;
    float thresholdDb = 15.0f;
    int minPersist = 5;
};
