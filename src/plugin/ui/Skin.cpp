#include "Skin.h"

#include "BinaryData.h"

namespace yrev::ui
{
    namespace
    {
        juce::Image load (const void* data, int size)
        {
            return juce::ImageFileFormat::loadFrom (data, (size_t) size);
        }
    } // namespace

    const Skin& Skin::get ()
    {
        // A temporary handle: the editor's own handle (YrevEditor::skinHandle) is what keeps the skin alive.
        return juce::SharedResourcePointer<Skin> ().get ();
    }

    Skin::Skin ()
    {
        background = load (BinaryData::panelbg_jpg, BinaryData::panelbg_jpgSize);
        knobCap = load (BinaryData::knobcap_png, BinaryData::knobcap_pngSize);

        key[0][0][0] = load (BinaryData::keysqoff_png, BinaryData::keysqoff_pngSize);
        key[0][0][1] = load (BinaryData::keysqoffpushed_png, BinaryData::keysqoffpushed_pngSize);
        key[0][1][0] = load (BinaryData::keysqon_png, BinaryData::keysqon_pngSize);
        key[0][1][1] = load (BinaryData::keysqonpushed_png, BinaryData::keysqonpushed_pngSize);
        key[1][0][0] = load (BinaryData::keyrectoff_png, BinaryData::keyrectoff_pngSize);
        key[1][0][1] = load (BinaryData::keyrectoffpushed_png, BinaryData::keyrectoffpushed_pngSize);
        key[1][1][0] = load (BinaryData::keyrecton_png, BinaryData::keyrecton_pngSize);
        key[1][1][1] = load (BinaryData::keyrectonpushed_png, BinaryData::keyrectonpushed_pngSize);

        labelFace = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedSemiBold_ttf,
                                                             (size_t) BinaryData::BarlowCondensedSemiBold_ttfSize);
        lcdFace = juce::Typeface::createSystemTypefaceFor (BinaryData::VT323Regular_ttf,
                                                           (size_t) BinaryData::VT323Regular_ttfSize);
    }

    juce::Font Skin::label (float size, float tracking) const
    {
        return juce::Font (juce::FontOptions (labelFace).withPointHeight (size)).withExtraKerningFactor (tracking);
    }

    juce::Font Skin::lcd (float size, float tracking) const
    {
        return juce::Font (juce::FontOptions (lcdFace).withPointHeight (size)).withExtraKerningFactor (tracking);
    }
} // namespace yrev::ui
