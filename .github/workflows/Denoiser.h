#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cmath>

extern "C"
{
#include <rnnoise.h>
}

// ---------------------------------------------------------------------------
// RNNoise travaille uniquement a 48 kHz, par blocs de 480 echantillons (10 ms).
// Si le logiciel tourne a une autre frequence (44,1 kHz par ex.), on convertit
// avant et apres avec un reechantillonneur cubique (Catmull-Rom) en flux continu.
// ---------------------------------------------------------------------------

class StreamResampler
{
public:
    void prepare (double srcRate, double dstRate)
    {
        ratio = srcRate / dstRate;
        buf.clear();
        buf.reserve (32768);
        buf.assign (3, 0.0f);
        pos = 1.0;
    }

    // Ajoute les echantillons convertis a la fin de `out` (ne vide pas `out`).
    void process (const float* in, int n, std::vector<float>& out)
    {
        buf.insert (buf.end(), in, in + n);

        while ((int) pos + 2 < (int) buf.size())
        {
            const int   i = (int) pos;
            const float f = (float) (pos - (double) i);
            const float ym1 = buf[(size_t) i - 1], y0 = buf[(size_t) i],
                        y1  = buf[(size_t) i + 1], y2 = buf[(size_t) i + 2];

            const float c1 = 0.5f * (y1 - ym1);
            const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            out.push_back (((c3 * f + c2) * f + c1) * f + y0);

            pos += ratio;
        }

        const int drop = (int) pos - 1;
        if (drop > 0)
        {
            buf.erase (buf.begin(), buf.begin() + drop);
            pos -= (double) drop;
        }
    }

private:
    std::vector<float> buf;
    double pos = 1.0, ratio = 1.0;
};

// File d'attente simple (premier entre, premier sorti)
class SampleFifo
{
public:
    void clear()                          { d.clear(); r = 0; }
    size_t size() const                   { return d.size() - r; }
    const float* data() const             { return d.data() + r; }
    void push1 (float v)                  { d.push_back (v); }
    void push (const float* p, size_t n)  { d.insert (d.end(), p, p + n); }
    void pushZeros (size_t n)             { d.insert (d.end(), n, 0.0f); }
    void reserve (size_t n)               { d.reserve (n); }

    void pop (size_t n)
    {
        r += n;
        if (r >= d.size())              { d.clear(); r = 0; }
        else if (r > 8192 && r * 2 > d.size()) { d.erase (d.begin(), d.begin() + (long) r); r = 0; }
    }

private:
    std::vector<float> d;
    size_t r = 0;
};

// Un canal audio passe dans RNNoise
class DenoiseChannel
{
public:
    DenoiseChannel() = default;
    DenoiseChannel (const DenoiseChannel&) = delete;
    DenoiseChannel& operator= (const DenoiseChannel&) = delete;
    ~DenoiseChannel() { if (state != nullptr) rnnoise_destroy (state); }

    static constexpr int frameSize = 480;   // RNNoise : 10 ms a 48 kHz

    void prepare (double hostRate, int maxBlock, int prefillSamples)
    {
        if (state != nullptr) rnnoise_destroy (state);
        state = rnnoise_create (nullptr);

        resample = std::abs (hostRate - 48000.0) > 1.0;
        if (resample)
        {
            up.prepare (hostRate, 48000.0);
            down.prepare (48000.0, hostRate);
        }

        const size_t cap = (size_t) maxBlock * 4 + 8192;
        up48.clear();    up48.reserve (cap);
        prod48.clear();  prod48.reserve (cap);
        prodHost.clear(); prodHost.reserve (cap);
        in48.clear();    in48.reserve (cap);
        hostOut.clear(); hostOut.reserve (cap);

        // Reserve de silence : evite de manquer d'echantillons quand la taille
        // des blocs du logiciel n'est pas un multiple de 480.
        hostOut.pushZeros ((size_t) prefillSamples);
    }

    // Traite n echantillons de `in` -> `out` (meme longueur, avec un retard fixe).
    void process (const float* in, float* out, int n)
    {
        if (state == nullptr) { std::copy (in, in + n, out); return; }

        // 1) vers 48 kHz
        const float* src = in;
        int srcN = n;
        if (resample)
        {
            up48.clear();
            up.process (in, n, up48);
            src  = up48.data();
            srcN = (int) up48.size();
        }

        // 2) RNNoise attend des valeurs echelle 16 bits (+/- 32768)
        for (int i = 0; i < srcN; ++i)
            in48.push1 (src[i] * 32768.0f);

        prod48.clear();
        float frameOut[frameSize];
        while (in48.size() >= (size_t) frameSize)
        {
            rnnoise_process_frame (state, frameOut, in48.data());
            in48.pop ((size_t) frameSize);
            for (int i = 0; i < frameSize; ++i)
                prod48.push_back (frameOut[i] * (1.0f / 32768.0f));
        }

        // 3) retour a la frequence du logiciel
        if (resample)
        {
            prodHost.clear();
            down.process (prod48.data(), (int) prod48.size(), prodHost);
            hostOut.push (prodHost.data(), prodHost.size());
        }
        else
        {
            hostOut.push (prod48.data(), prod48.size());
        }

        // 4) on rend exactement n echantillons
        const int take = (int) std::min ((size_t) n, hostOut.size());
        std::copy (hostOut.data(), hostOut.data() + take, out);
        if (take < n) std::fill (out + take, out + n, 0.0f);     // (ne devrait pas arriver)
        hostOut.pop ((size_t) take);
    }

private:
    DenoiseState* state = nullptr;
    bool resample = false;
    StreamResampler up, down;
    SampleFifo in48, hostOut;
    std::vector<float> up48, prod48, prodHost;
};

// Ligne a retard (pour realigner le son d'origine avec le son debruite)
class DelayLine
{
public:
    void prepare (int delaySamples)
    {
        len = std::max (1, delaySamples);
        ring.assign ((size_t) len, 0.0f);
        w = 0;
    }

    float process (float x)
    {
        const float y = ring[(size_t) w];
        ring[(size_t) w] = x;
        if (++w >= len) w = 0;
        return y;
    }

private:
    std::vector<float> ring;
    int len = 1, w = 0;
};
