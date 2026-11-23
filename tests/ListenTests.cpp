// Static listening tests (hidden: run with `YREV57Tests "[.listen]"`). Not part of the test count.
//
//   YREV57Tests "[.listen]"    renders every test below and writes references/listening/index.html
//   YREV_LISTEN_DIR=dir        output folder (default references/listening, git-ignored)
//   YREV_SAMPLES_DIR=dir       the one-shots (default references/samples, git-ignored; commercial samples, never
//                              committed or shipped)
//
// Each variant is a preset loaded through the whole plugin (the processor, as a host would), with any overrides, fed
// the four one-shots in a row (vocal, stomp, clap, snare), each followed by a silence long enough for the tail. The
// noise seed is frozen and the era settings shared, so renders repeat. Output: 24-bit FLAC at 44.1 kHz, plus a dry
// reference and an index page with players and what to listen for. Tests that need the plugin moved while it plays
// (the era crossfade, per-era memory, knob moves) are not here: they are for the live listening pass.
//
// Only the open choices are left (which variant becomes the default); the other groups were heard by the maintainer
// on 2026-11-22 and removed (01–07, 09, 12, 14 passed; 10, 11, 13, 15 kept the defaults; 08 decided the FX period
// defaults). Group numbers are kept so docs still match.
#include "plugin/PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace
{
    // ---- Config ----
    constexpr double kFs = 44100.0;
    constexpr int kBlock = 256;
    constexpr double kLeadSeconds = 0.2; // silence before the first hit (the engine starts fresh)
    constexpr int kBits = 24;
    const char* const kSamples[] = {"VOX_UGV_vocal_gang_oneshot_hey_gang.wav", "TS_IFD_stomp_crisp.wav",
                                    "OLIVER_clap_group_punch.wav",
                                    "TS_PAUL_MABURY_snare_one_shot_smack_beauty_nat_h1.wav"};

    using Values = std::vector<std::pair<const char*, float>>;
    struct Variant
    {
        const char* label; // shown on the page
        const char* bank;
        const char* preset;
        Values values{};      // parameter overrides (real values) after the preset loads
        Values settings{};    // plugin settings (state properties, "1" when the value is non-zero)
        bool panLeft = false; // the input on the left channel only
    };
    struct Test
    {
        const char* id;
        const char* title;
        const char* listenFor;
        double gapSeconds; // after each hit
        std::vector<Variant> variants;
    };

    std::vector<Test> tests ()
    {
        // Every group so far has been heard and closed (2026-11-22, ROADMAP M5); add new A/B groups here.
        return {};
    }

    juce::File dirFrom (const char* env, const char* fallback)
    {
        if (const char* v = std::getenv (env))
            return juce::File::getCurrentWorkingDirectory ().getChildFile (v);
        // the tests run from the build folder or the repo root: find the repo root by its CLAUDE.md
        auto d = juce::File::getCurrentWorkingDirectory ();
        for (int i = 0; i < 4 && !d.getChildFile ("CLAUDE.md").existsAsFile (); ++i)
            d = d.getParentDirectory ();
        return d.getChildFile (fallback);
    }

    // the four one-shots in a row, each followed by gapSeconds of silence
    juce::AudioBuffer<float> buildInput (const juce::File& dir, double gapSeconds, bool panLeft)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats ();
        std::vector<juce::AudioBuffer<float>> parts;
        int total = (int) (kLeadSeconds * kFs);
        for (const char* name : kSamples)
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (dir.getChildFile (name)));
            REQUIRE (r != nullptr);
            REQUIRE (std::abs (r->sampleRate - kFs) < 1.0); // the samples are 44.1 kHz; no resampling here
            juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
            r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
            if (r->numChannels == 1)
                b.copyFrom (1, 0, b, 0, 0, b.getNumSamples ());
            if (panLeft)
            {
                b.addFrom (0, 0, b, 1, 0, b.getNumSamples ());
                b.applyGain (0, 0, b.getNumSamples (), 0.5f);
                b.clear (1, 0, b.getNumSamples ());
            }
            total += b.getNumSamples () + (int) (gapSeconds * kFs);
            parts.push_back (std::move (b));
        }
        juce::AudioBuffer<float> in (2, total);
        in.clear ();
        int pos = (int) (kLeadSeconds * kFs);
        for (auto& b : parts)
        {
            for (int c = 0; c < 2; ++c)
                in.copyFrom (c, pos, b, c, 0, b.getNumSamples ());
            pos += b.getNumSamples () + (int) (gapSeconds * kFs);
        }
        return in;
    }

    juce::AudioBuffer<float> render (const Variant& v, const juce::AudioBuffer<float>& in)
    {
        YrevProcessor p;
        auto& st = p.apvts.state;
        st.setProperty ("noiseSeed", "frozen", nullptr);
        st.setProperty ("eraMemory", "shared", nullptr);
        for (const auto& [key, val] : v.settings)
            st.setProperty (key, val != 0.0f ? "1" : "0", nullptr);
        int idx = -1;
        for (int i = 0; i < p.presets.size (); ++i)
            if (p.presets.at (i).name == v.preset && p.presets.at (i).bank == v.bank)
                idx = i;
        INFO (v.bank << "/" << v.preset);
        REQUIRE (idx >= 0);
        p.presets.load (idx);
        for (const auto& [id, val] : v.values)
        {
            auto* prm = p.apvts.getParameter (id);
            REQUIRE (prm != nullptr);
            prm->setValueNotifyingHost (prm->convertTo0to1 (val));
        }
        p.enforceEraLimits ();
        p.setPlayConfigDetails (2, 2, kFs, kBlock);
        p.prepareToPlay (kFs, kBlock);
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < out.getNumSamples (); pos += kBlock)
        {
            const int len = std::min (kBlock, out.getNumSamples () - pos);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers (), 2, pos, len);
            p.processBlock (view, midi);
        }
        return out;
    }

    void writeFlac (const juce::File& f, const juce::AudioBuffer<float>& b)
    {
        f.deleteFile ();
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream ());
        REQUIRE (os != nullptr);
        const auto opts = juce::AudioFormatWriterOptions{}
                              .withSampleRate (kFs)
                              .withNumChannels (2)
                              .withBitsPerSample (kBits)
                              .withQualityOptionIndex (flac.getQualityOptions ().size () - 1);
        auto w = flac.createWriterFor (os, opts); // takes the stream when it succeeds
        REQUIRE (w != nullptr);
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples ());
    }

    juce::String slug (const juce::String& s)
    {
        return s.toLowerCase ().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789 ").trim ().replace (" ", "-");
    }
} // namespace

TEST_CASE ("render the static listening tests", "[.listen]")
{
    const auto samples = dirFrom ("YREV_SAMPLES_DIR", "references/samples");
    const auto out = dirFrom ("YREV_LISTEN_DIR", "references/listening");
    REQUIRE (samples.isDirectory ());
    out.createDirectory ();

    juce::String html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\"><title>YREV57 listening tests</title><style>"
            ":root{--bg:#f6f5f2;--fg:#1d1d1f;--dim:#5c5c60;--card:#fff;--line:#ddd}"
            "@media (prefers-color-scheme:dark){:root{--bg:#161617;--fg:#ececed;--dim:#a1a1a6;--card:#222224;"
            "--line:#3a3a3c}}body{background:var(--bg);color:var(--fg);font:15px/1.5 -apple-system,system-ui,"
            "sans-serif;max-width:900px;margin:0 auto;padding:16px}h1{font-size:22px}h2{font-size:17px;margin:0 0 "
            "4px}section{background:var(--card);border:1px solid var(--line);border-radius:6px;padding:14px;"
            "margin:14px 0}p{color:var(--dim);margin:4px 0 10px}.row{display:flex;align-items:center;gap:12px;"
            "margin:6px 0;flex-wrap:wrap}.row span{min-width:230px}audio{width:100%;max-width:520px;height:34px}"
            "</style></head><body><h1>YREV57 listening tests</h1><p>Rendered "
         << juce::Time::getCurrentTime ().toString (true, true) << " from YREV57 " << JucePlugin_VersionString
         << ". Each file plays the vocal, the stomp, the clap and the snare in turn, each with its tail. Use "
            "headphones or monitors at a fixed level, and listen to each group as a set.</p>";

    {
        const auto dry = buildInput (samples, 2.0, false);
        writeFlac (out.getChildFile ("00-dry.flac"), dry);
        html << "<section><h2>Dry reference</h2><div class=\"row\"><span>The four one-shots, untouched</span><audio "
                "controls preload=\"none\" src=\"00-dry.flac\"></audio></div></section>";
    }

    for (const auto& t : tests ())
    {
        html << "<section><h2>" << juce::String (t.title).replace ("&", "&amp;") << "</h2><p>"
             << juce::String (t.listenFor).replace ("&", "&amp;") << "</p>";
        for (const auto& v : t.variants)
        {
            const auto in = buildInput (samples, t.gapSeconds, v.panLeft);
            const auto file = juce::String (t.id) + "_" + slug (v.label) + ".flac";
            writeFlac (out.getChildFile (file), render (v, in));
            html << "<div class=\"row\"><span>" << juce::String (v.label).replace ("&", "&amp;")
                 << "</span><audio controls preload=\"none\" src=\"" << file << "\"></audio></div>";
        }
        html << "</section>";
    }
    html << "</body></html>";
    REQUIRE (out.getChildFile ("index.html").replaceWithText (html));
}
