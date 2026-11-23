#include "Presets.h"

#include "FactoryPresets.h"
#include "Parameters.h"

namespace yrev
{
    namespace
    {
        constexpr float kSame = 1.0e-3f; // tolerance when comparing stored and current values
        const char* const kNotInPresets[] = {id::bypass,   id::mute,     id::wetSolo,   id::inLevel,
                                             id::outLevel, id::wetInput, id::outPhaseR, id::eqView};

        juce::String safeFileName (const juce::String& name)
        {
            auto s =
                name.trim ().retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_().+");
            return s.isEmpty () ? juce::String ("Untitled") : s;
        }
    } // namespace

    bool PresetManager::isPresetParam (const juce::String& pid)
    {
        for (auto* n : kNotInPresets)
            if (pid == n)
                return false;
        return true;
    }

    PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        userDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                      .getChildFile ("YREV57")
                      .getChildFile ("Presets");
        rebuild ();
        restoreFromState ();
    }

    void PresetManager::setUserDirectory (const juce::File& dir)
    {
        userDir = dir;
        rebuild ();
    }

    // Presets are keyed by bank and name ("REV5/Large Hall"): names repeat across the read-only banks.
    static juce::String keyOf (const juce::String& bank, const juce::String& name)
    {
        return bank + "/" + name;
    }

    juce::String PresetManager::currentKey () const
    {
        return currentIndex >= 0 && currentIndex < size ()
                   ? keyOf (presets[(size_t) currentIndex].bank, presets[(size_t) currentIndex].name)
                   : juce::String ();
    }

    void PresetManager::rebuild ()
    {
        const auto keepKey = currentKey ();
        presets.clear ();
        for (const auto bank : {"FACTORY", "COMBI", "REV7", "REV5", "SECRET"})
            for (const auto& f : kFactoryPresets)
                if (juce::String (f.bank) == bank)
                {
                    Preset p;
                    p.name = f.name;
                    p.bank = f.bank;
                    for (int i = 0; i < f.count; ++i)
                        p.values[f.values[i].id] = f.values[i].value;
                    presets.push_back (std::move (p));
                }

        if (userDir.isDirectory ())
        {
            auto files = userDir.findChildFiles (juce::File::findFiles, false, "*.xml");
            files.sort ();
            for (const auto& f : files)
                if (auto xml = juce::XmlDocument::parse (f))
                    if (xml->hasTagName ("YREV57Preset"))
                    {
                        Preset p;
                        p.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension ());
                        p.bank = "USER";
                        p.user = true;
                        for (auto* e : xml->getChildWithTagNameIterator ("P"))
                            p.values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("v");
                        presets.push_back (std::move (p));
                    }
        }

        currentIndex = -1;
        for (size_t i = 0; i < presets.size (); ++i)
            if (keyOf (presets[i].bank, presets[i].name) == keepKey)
                currentIndex = (int) i;
    }

    juce::String PresetManager::currentName () const
    {
        return currentIndex >= 0 && currentIndex < size () ? presets[(size_t) currentIndex].name : juce::String ();
    }

    void PresetManager::remember ()
    {
        apvts.state.setProperty ("preset", currentKey (), nullptr);
    }

    void PresetManager::restoreFromState ()
    {
        // "BANK/name"; a bare name (state saved before banks were keyed) takes the first preset of that name
        const auto key = apvts.state.getProperty ("preset").toString ();
        currentIndex = -1;
        if (key.isEmpty ())
            return;
        for (size_t i = 0; i < presets.size () && currentIndex < 0; ++i)
            if (keyOf (presets[i].bank, presets[i].name) == key)
                currentIndex = (int) i;
        for (size_t i = 0; i < presets.size () && currentIndex < 0; ++i)
            if (presets[i].name == key)
                currentIndex = (int) i;
    }

    std::map<juce::String, float> PresetManager::snapshot () const
    {
        std::map<juce::String, float> v;
        for (auto* p : apvts.processor.getParameters ())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (isPresetParam (r->paramID))
                    v[r->paramID] = r->convertFrom0to1 (r->getValue ());
        return v;
    }

    std::map<juce::String, float> PresetManager::effectiveValues (const Preset& p) const
    {
        std::map<juce::String, float> v;
        for (auto* prm : apvts.processor.getParameters ())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (isPresetParam (r->paramID))
                {
                    const auto it = p.values.find (r->paramID);
                    v[r->paramID] = it != p.values.end () ? it->second : r->convertFrom0to1 (r->getDefaultValue ());
                }
        return v;
    }

    void PresetManager::setParam (const juce::String& pid, float real)
    {
        if (auto* r = apvts.getParameter (pid))
        {
            const float n = r->convertTo0to1 (real);
            if (std::abs (r->getValue () - n) > 1.0e-6f)
            {
                r->beginChangeGesture ();
                r->setValueNotifyingHost (n);
                r->endChangeGesture ();
            }
        }
    }

    void PresetManager::apply (const std::map<juce::String, float>& values, bool hooks)
    {
        beginLoad ();
        if (hooks && onBeforeApply)
            onBeforeApply ();
        // era first, so anything that depends on it sees the right state
        if (const auto it = values.find (id::era); it != values.end ())
            setParam (id::era, it->second);
        for (const auto& [pid, v] : values)
            if (pid != id::era)
                setParam (pid, v);
        if (hooks && onAfterApply)
            onAfterApply ();
        endLoad ();
    }

    void PresetManager::load (int index)
    {
        if (index < 0 || index >= size ())
            return;
        compareOn = false;
        apply (effectiveValues (presets[(size_t) index]));
        currentIndex = index;
        remember ();
    }

    void PresetManager::init ()
    {
        compareOn = false;
        std::map<juce::String, float> v;
        for (auto* prm : apvts.processor.getParameters ())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (isPresetParam (r->paramID))
                    v[r->paramID] = r->convertFrom0to1 (r->getDefaultValue ());
        apply (v);
        currentIndex = -1;
        remember ();
    }

    bool PresetManager::isEdited () const
    {
        const auto cur = snapshot ();
        if (currentIndex < 0)
            return false;
        const auto ref = effectiveValues (presets[(size_t) currentIndex]);
        for (const auto& [k, v] : ref)
        {
            const auto it = cur.find (k);
            if (it != cur.end () && std::abs (it->second - v) > kSame * juce::jmax (1.0f, std::abs (v)))
                return true;
        }
        return false;
    }

    void PresetManager::toggleCompare ()
    {
        if (currentIndex < 0)
            return;
        if (!compareOn)
        {
            editedSnapshot = snapshot ();
            apply (effectiveValues (presets[(size_t) currentIndex]));
            compareOn = true;
        }
        else
        {
            apply (editedSnapshot);
            compareOn = false;
        }
    }

    juce::File PresetManager::fileFor (const juce::String& name) const
    {
        return userDir.getChildFile (safeFileName (name) + ".xml");
    }

    static bool writePreset (const juce::File& f, const juce::String& name, const std::map<juce::String, float>& values)
    {
        juce::XmlElement xml ("YREV57Preset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("version", 1);
        for (const auto& [k, v] : values)
        {
            auto* e = xml.createNewChildElement ("P");
            e->setAttribute ("id", k);
            e->setAttribute ("v", (double) v);
        }
        f.getParentDirectory ().createDirectory ();
        return xml.writeTo (f);
    }

    bool PresetManager::storeAs (const juce::String& name)
    {
        const auto clean = name.trim ();
        if (clean.isEmpty () || userDir == juce::File ())
            return false;
        for (const auto& p : presets)
            if (!p.user && p.name.equalsIgnoreCase (clean))
                return false; // factory names are taken
        if (!writePreset (fileFor (clean), clean, snapshot ()))
            return false;
        rebuild ();
        for (size_t i = 0; i < presets.size (); ++i)
            if (presets[i].user && presets[i].name == clean)
                currentIndex = (int) i;
        compareOn = false;
        remember ();
        return true;
    }

    bool PresetManager::storeCurrent ()
    {
        if (currentIndex < 0 || !presets[(size_t) currentIndex].user)
            return false;
        return storeAs (presets[(size_t) currentIndex].name);
    }

    bool PresetManager::rename (int index, const juce::String& newName)
    {
        if (index < 0 || index >= size () || !presets[(size_t) index].user || newName.trim ().isEmpty ())
            return false;
        const auto old = presets[(size_t) index];
        for (const auto& p : presets)
            if (p.name.equalsIgnoreCase (newName.trim ()) && &p != &presets[(size_t) index])
                return false;
        if (!writePreset (fileFor (newName.trim ()), newName.trim (), old.values))
            return false;
        if (fileFor (old.name) != fileFor (newName.trim ()))
            fileFor (old.name).deleteFile ();
        const bool wasCurrent = index == currentIndex;
        const auto keep = wasCurrent ? keyOf ("USER", newName.trim ()) : currentKey ();
        rebuild ();
        currentIndex = -1;
        for (size_t i = 0; i < presets.size (); ++i)
            if (keyOf (presets[i].bank, presets[i].name) == keep)
                currentIndex = (int) i;
        remember ();
        return true;
    }

    bool PresetManager::remove (int index)
    {
        if (index < 0 || index >= size () || !presets[(size_t) index].user)
            return false;
        const auto keep = index == currentIndex ? juce::String () : currentKey ();
        fileFor (presets[(size_t) index].name).deleteFile ();
        rebuild ();
        currentIndex = -1;
        for (size_t i = 0; i < presets.size (); ++i)
            if (keep.isNotEmpty () && keyOf (presets[i].bank, presets[i].name) == keep)
                currentIndex = (int) i;
        remember ();
        return true;
    }

    bool PresetManager::exportTo (int index, const juce::File& file) const
    {
        if (index < 0 || index >= size ())
            return false;
        return writePreset (file, presets[(size_t) index].name, effectiveValues (presets[(size_t) index]));
    }

    bool PresetManager::importFrom (const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || !xml->hasTagName ("YREV57Preset"))
            return false;
        std::map<juce::String, float> values;
        for (auto* e : xml->getChildWithTagNameIterator ("P"))
            values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("v");
        auto name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension ()).trim ();
        if (name.isEmpty () || values.empty ())
            return false;
        for (const auto& p : presets)
            if (p.name.equalsIgnoreCase (name))
                name += " (imported)";
        if (!writePreset (fileFor (name), name, values))
            return false;
        rebuild ();
        return true;
    }
} // namespace yrev
