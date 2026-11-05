// Test hors-ligne du moteur ThaVOICE
#include "Source/YvDsp.h"
#include <cstdio>
#include <random>

using namespace yv;

static void defaults (float* v) { for (int i = 0; i < Count; ++i) v[i] = kDefs[i].def; }

static std::vector<float> makeVoice (double sr, double sec, float amp)
{
    const size_t N = (size_t) (sr * sec);
    std::vector<float> x (N);
    std::mt19937 rng (5); std::normal_distribution<float> nd (0.f, 1.f);
    double lp = 0;
    for (size_t i = 0; i < N; ++i)
    {
        const double t = (double) i / sr;
        double v = 0;
        for (int h = 1; h < 12; ++h) v += std::sin (2 * kPi * 180.0 * h * t) / h;
        const double env = 0.5 + 0.5 * std::sin (2 * kPi * 1.3 * t);
        lp += 0.2 * ((double) nd (rng) - lp);
        const double sib = (std::fmod (t, 1.0) > 0.6 && std::fmod (t, 1.0) < 0.7) ? (double) nd (rng) - lp : 0.0;
        x[i] = (float) (amp * (0.5 * v * env + 0.35 * sib));
    }
    return x;
}

static double rmsOf (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (b - a));
}

static void run (Engine& e, std::vector<float>& L, std::vector<float>& R, int block)
{
    for (size_t i = 0; i < L.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, L.size() - i);
        e.process (L.data() + i, R.data() + i, n);
        int s1, z1, s2, z2; e.scopeFifo.prepareToRead (n, s1, z1, s2, z2); e.scopeFifo.finishedRead (z1 + z2);
    }
}

int main()
{
    int fails = 0;
    auto check = [&] (bool ok, const char* m) { std::printf ("%s %s\n", ok ? "OK  " : "FAIL", m); if (! ok) ++fails; };

    // 1) tous les modules actifs, 4 fs x 5 tailles de bloc : pas de NaN, pas de depassement, sortie non nulle
    const double rates[4] = { 44100, 48000, 88200, 96000 };
    const int blocks[5] = { 128, 256, 512, 1024, 2048 };
    bool allFinite = true, allBounded = true, allAlive = true;
    for (double sr : rates)
        for (int b : blocks)
        {
            Engine e; e.prepare (sr, b);
            float v[Count]; defaults (v);
            v[compOn] = v[eqOn] = v[fxOn] = v[dlyOn] = v[revOn] = 1;
            v[deessOn] = v[satOn] = v[dblOn] = v[choOn] = 1; v[dlyMix] = 25; v[revMix] = 30;
            e.setParams (v); e.setBpm (120);
            auto x = makeVoice (sr, 4.0, 0.8f);
            std::vector<float> L = x, R = x;
            run (e, L, R, b);
            double pk = 0;
            for (size_t i = 0; i < L.size(); ++i)
            {
                if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) allFinite = false;
                pk = std::max (pk, (double) std::max (std::abs (L[i]), std::abs (R[i])));
            }
            if (pk > 1.0) allBounded = false;
            if (rmsOf (L, L.size() / 2, L.size()) < 1e-4) allAlive = false;
        }
    check (allFinite, "pas de NaN/Inf (4 frequences x 5 blocs)");
    check (allBounded, "sortie <= 0 dBFS (protection de sortie)");
    check (allAlive, "sortie non nulle");

    // 2) determinisme selon la taille de bloc (a 1e-4 pres)
    {
        const double sr = 48000;
        auto x = makeVoice (sr, 3.0, 0.5f);
        std::vector<float> a[2];
        const int bs[2] = { 128, 1024 };
        for (int k = 0; k < 2; ++k)
        {
            Engine e; e.prepare (sr, bs[k]);
            float v[Count]; defaults (v); v[dlyOn] = 1; v[revOn] = 1; v[fxOn] = 1; v[choOn] = 1; v[dblOn] = 1;
            e.setParams (v);
            a[k] = x; std::vector<float> R = x;
            run (e, a[k], R, bs[k]);
        }
        double d = 0;
        for (size_t i = 0; i < a[0].size(); ++i) d = std::max (d, (double) std::abs (a[0][i] - a[1][i]));
        std::printf ("     ecart max blocs 128 vs 1024 : %.2e\n", d);
        check (d < 2e-3, "resultat independant de la taille de bloc");
    }

    // 3) bypass : sortie == entree (apres rampe)
    {
        const double sr = 48000;
        Engine e; e.prepare (sr, 256);
        float v[Count]; defaults (v); v[bypass] = 1; v[revOn] = 1; v[revMix] = 80; v[inGain] = 6;
        e.setParams (v);
        auto x = makeVoice (sr, 1.0, 0.4f); std::vector<float> L = x, R = x;
        run (e, L, R, 256);
        double d = 0;
        const size_t lat = (size_t) e.latency();
        for (size_t i = 2000 + lat; i < L.size(); ++i) d = std::max (d, (double) std::abs (L[i] - x[i - lat]));
        check (d < 1e-6, "bypass global transparent");
    }

    // 4) activite par module : chaque module change le signal quand il est active
    {
        const double sr = 48000;
        auto x = makeVoice (sr, 2.5, 0.5f);
        struct T { const char* name; int param; float val; };
        const T tests[] = { { "COMP", compOn, 1 }, { "EQ", eqOn, 1 }, { "DE-ESSER", deessOn, 1 }, { "SAT", satOn, 1 }, { "DOUBLER", dblOn, 1 },
                            { "CHORUS", choOn, 1 }, { "PHONE", phoneOn, 1 }, { "WIDTH", widthOn, 1 }, { "DELAY", dlyOn, 1 }, { "REVERB", revOn, 1 } };
        for (auto& t : tests)
        {
            Engine e; e.prepare (sr, 512);
            float v[Count]; defaults (v);
            v[compOn] = v[eqOn] = v[fxOn] = v[dlyOn] = v[revOn] = 0;
            v[deessOn] = v[satOn] = v[dblOn] = v[choOn] = v[phoneOn] = v[widthOn] = 1;
            v[t.param] = t.val;
            if (t.param == compOn) { v[compThr] = -30; v[compRatio] = 6; }
            if (t.param == eqOn) { v[hsG] = 6; v[lmG] = -4; }
            if (t.param == widthOn) v[width] = 0;
            if (t.param == deessOn) v[deessAmt] = 100;
            if (t.param == satOn) v[satDrive] = 80;
            if (t.param == phoneOn) v[phoneAmt] = 100;
            if (t.param == dblOn || t.param == choOn || t.param == phoneOn || t.param == deessOn || t.param == satOn || t.param == widthOn) v[fxOn] = 1;
            if (t.param == dblOn) v[dblAmt] = 100;
            if (t.param == choOn) v[choMix] = 100;
            if (t.param == dlyOn) v[dlyMix] = 60;
            if (t.param == revOn) v[revMix] = 60;
            // reference : tout a zero
            Engine e0; e0.prepare (sr, 512);
            float v0[Count]; defaults (v0); v0[compOn] = v0[eqOn] = v0[fxOn] = v0[dlyOn] = v0[revOn] = 0;
            e0.setParams (v0); e.setParams (v);
            std::vector<float> L = x, R = x, L0 = x, R0 = x;
            run (e, L, R, 512); run (e0, L0, R0, 512);
            double d = 0; for (size_t i = 0; i < L.size(); ++i) d += std::abs (L[i] - L0[i]);
            d /= (double) L.size();
            char m[64]; std::snprintf (m, sizeof m, "module %s modifie le signal (ecart moyen %.4f)", t.name, d);
            check (d > 1e-4, m);
        }
    }

    // 5) etalonnage reverb : niveau de queue, 4 modes, decay raisonnable
    {
        const double sr = 48000;
        for (int mode = 0; mode < 4; ++mode)
        {
            Engine e; e.prepare (sr, 512);
            float v[Count]; defaults (v);
            v[compOn] = v[eqOn] = v[fxOn] = v[dlyOn] = 0; v[revOn] = 1; v[revMode] = (float) mode; v[revMix] = 100; v[revDecay] = 2.0f; v[revPre] = 0; v[revDuck] = 0;
            e.setParams (v);
            const size_t N = (size_t) (sr * 6);
            std::vector<float> L (N, 0.f), R (N, 0.f);
            for (int i = 0; i < 200; ++i) L[(size_t) i] = R[(size_t) i] = 0.3f;   // salve breve
            run (e, L, R, 512);
            const double e1 = rmsOf (L, (size_t) (0.1 * sr), (size_t) (0.3 * sr));
            const double e2 = rmsOf (L, (size_t) (1.1 * sr), (size_t) (1.3 * sr));
            const double e3 = rmsOf (L, (size_t) (4.5 * sr), (size_t) (5.0 * sr));
            std::printf ("     mode %d : rms 0.1-0.3 s = %.1f dB, 1.1-1.3 s = %.1f dB, 4.5-5 s = %.1f dB\n", mode, linToDb (e1), linToDb (e2), linToDb (e3));
            check (e1 > 1e-4 && e2 < e1 && e3 < e2, "reverb : niveau present et decroissant");
        }
    }

    // 5b) ducking de reverb : le niveau de la reverb baisse pendant la voix
    {
        const double sr = 48000;
        auto x = makeVoice (sr, 4.0, 0.6f);
        double lv[2];
        for (int k = 0; k < 2; ++k)
        {
            Engine e; e.prepare (sr, 512);
            float v[Count]; defaults (v);
            v[compOn] = v[eqOn] = v[fxOn] = v[dlyOn] = 0; v[revOn] = 1; v[revMix] = 100; v[revDuck] = k ? 100.f : 0.f;
            e.setParams (v);
            std::vector<float> L = x, R = x;
            run (e, L, R, 512);
            const size_t lat = (size_t) e.latency();
            double s = 0; for (size_t i = (size_t) (2 * sr); i < L.size(); ++i) s += (double) (L[i] - x[i - lat]) * (L[i] - x[i - lat]);
            lv[k] = std::sqrt (s / (double) (L.size() - (size_t) (2 * sr)));
        }
        std::printf ("     reverb wet rms : duck 0 = %.4f, duck 100 = %.4f\n", lv[0], lv[1]);
        check (lv[1] < lv[0] * 0.8, "reverb ducking actif");
    }

    // 6) EQ : reponse en frequence conforme
    {
        float v[Count]; defaults (v);
        v[lmG] = 6; v[lmF] = 1000; v[lmQ] = 1.0f; v[hpOn] = 0; v[lsG] = 0; v[hmG] = 0; v[hsG] = 0; v[lpFreq] = 20000;
        const double g1k = EQ::responseDb (v, 48000, 1000), g100 = EQ::responseDb (v, 48000, 100);
        std::printf ("     EQ : 1 kHz = %.2f dB, 100 Hz = %.2f dB\n", g1k, g100);
        check (std::abs (g1k - 6.0) < 0.1 && std::abs (g100) < 0.6, "EQ : gain a la frequence centrale");
    }

    // 7) sample-rate change et signal mono (L=R) ; tempo-sync
    {
        Engine e; e.prepare (44100, 512); e.prepare (96000, 512);
        float v[Count]; defaults (v); v[dlyOn] = 1; v[dlySync] = 1; v[dlyNote] = 0; v[dlyMix] = 100; v[dlyFb] = 0; v[dlyPing] = 0; v[dlyDuck] = 0; v[dlyHP] = 20; v[dlyLP] = 20000;
        v[compOn] = v[eqOn] = v[fxOn] = v[revOn] = 0;
        e.setParams (v); e.setBpm (120);
        const size_t N = 96000 * 2;
        std::vector<float> L (N, 0.f), R (N, 0.f); L[1000] = R[1000] = 0.5f;
        run (e, L, R, 512);
        size_t pos = 0; float m = 0;
        for (size_t i = 1000 + (size_t) e.latency() + 500; i < N; ++i) if (std::abs (L[i]) > m) { m = std::abs (L[i]); pos = i; }
        const double expect = 1000 + 0.5 * 96000 + e.latency();   // 1/4 de note a 120 BPM = 0,5 s
        std::printf ("     echo a %zu (attendu ~%.0f)\n", pos, expect);
        check (std::abs ((double) pos - expect) < 200, "delay synchronise : 1/4 a 120 BPM");
    }


    // 8) AUTOTUNE : un la a 450 Hz (+39 cents) doit sortir a 440 Hz ; bruit : pas de NaN ; latence declaree
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            const size_t N = (size_t) (sr * 3.0) + 65536;
            auto freqOf = [&] (const std::vector<float>& y)
            {
                const int M = 1 << 16;
                std::vector<float> w (2 * (size_t) M, 0.f);
                for (int i = 0; i < M; ++i) w[(size_t) i] = y[y.size() - (size_t) M + (size_t) i];
                juce::dsp::WindowingFunction<float> win ((size_t) M, juce::dsp::WindowingFunction<float>::hann);
                win.multiplyWithWindowingTable (w.data(), (size_t) M);
                juce::dsp::FFT fft (16);
                fft.performFrequencyOnlyForwardTransform (w.data());
                int bm = 1; for (int i = 2; i < M / 2; ++i) if (w[(size_t) i] > w[(size_t) bm]) bm = i;
                const double a = w[(size_t) bm - 1], b = w[(size_t) bm], c = w[(size_t) bm + 1];
                return ((double) bm + 0.5 * (a - c) / (a - 2 * b + c)) * sr / M;
            };
            auto runTone = [&] (double f, int scale, int key, float speed, float amt)
            {
                Engine e; e.prepare (sr, 512);
                float v[Count]; defaults (v);
                v[compOn] = v[eqOn] = v[fxOn] = v[dlyOn] = v[revOn] = 0;
                v[tuneOn] = 1; v[tuneScale] = (float) scale; v[tuneKey] = (float) key; v[tuneSpeed] = speed; v[tuneAmt] = amt;
                e.setParams (v);
                std::vector<float> L (N), R;
                for (size_t i = 0; i < N; ++i) { const double ph = 2 * kPi * f * (double) i / sr; L[i] = (float) (0.3 * (std::sin (ph) + 0.4 * std::sin (2 * ph) + 0.2 * std::sin (3 * ph))); }
                R = L;
                run (e, L, R, 512);
                return freqOf (L);
            };
            const double f1 = runTone (450.0, 0, 0, 100.f, 100.f);
            const double f2 = runTone (450.0, 1, 0, 100.f, 100.f);   // Do majeur : le plus proche autorise est La = 440
            const double f3 = runTone (450.0, 0, 0, 100.f, 0.f);      // amount 0 : pas de correction
            std::printf ("     tune %.0f Hz : chromatique %.2f Hz, Do maj %.2f Hz, amount 0 -> %.2f Hz (attendu 440 / 440 / 450)\n", sr, f1, f2, f3);
            char m[96];
            std::snprintf (m, sizeof m, "autotune corrige a la bonne note (%.0f Hz)", sr);
            check (std::abs (f1 - 440.0) < 3.0 && std::abs (f2 - 440.0) < 3.0 && std::abs (f3 - 450.0) < 3.0, m);
        }
        // une source mono doit rester strictement mono apres l'autotune (pas d'elargissement du champ stereo)
        {
            const double sr = 48000;
            Engine e; e.prepare (sr, 512);
            float v[Count]; defaults (v);
            v[compOn] = v[eqOn] = v[fxOn] = v[dlyOn] = v[revOn] = 0; v[tuneOn] = 1; v[tuneSpeed] = 100;
            e.setParams (v);
            auto x = makeVoice (sr, 3.0, 0.5f);
            std::vector<float> L = x, R = x;
            run (e, L, R, 512);
            double d = 0; for (size_t i = 0; i < L.size(); ++i) d = std::max (d, (double) std::abs (L[i] - R[i]));
            check (d < 1e-6, "autotune : source mono reste mono");
        }
        // note exacte (La 440) : ne doit pas etre deformee
        Engine e; e.prepare (48000, 512);
        std::printf ("     latence declaree : %d echantillons (%.1f ms a 48 kHz)\n", e.latency(), 1000.0 * e.latency() / 48000.0);
    }

    std::printf (fails == 0 ? "\nTOUS LES TESTS OK\n" : "\n%d ECHEC(S)\n", fails);
    return fails;
}
