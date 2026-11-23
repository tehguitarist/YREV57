#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace yrev::ui
{
    // Images and fonts for the front-panel skin (assets/ui, assets/fonts, compiled into BinaryData).
    //
    // The skin is shared by the open editors (juce::SharedResourcePointer) and freed when the last editor closes. It must NOT
    // be a function-local or namespace-scope static: a static outlives the editor and is destroyed when the plug-in DLL is
    // unloaded, under the Windows loader lock. JUCE 9's Direct2D images release GPU resources in their destructor, and the
    // graphics driver can wait on its own threads, which need that same lock, so the host hangs on closing the plug-in
    // (REAPER "Unloading track", Gig Performer; reported against Tommy, tehguitarist/Tommy#2).
    //
    // YrevEditor holds the handle that keeps the skin alive. get() returns the shared skin and is valid only while an editor
    // exists, which is the only time anything paints with it.
    class Skin
    {
    public:
        static const Skin& get ();

        Skin (); // public only because juce::SharedResourcePointer constructs it; use get()

        juce::Image background, knobCap;
        juce::Image key[2][2][2]; // [rect][on][pushed]

        // Sizes are CSS-style pixels from the prototype (design units).
        juce::Font label (float size, float tracking = 0.06f) const;
        juce::Font lcd (float size, float tracking = 0.06f) const;

    private:
        juce::Typeface::Ptr labelFace, lcdFace;
    };

    // Palette (from the prototype's CSS variables)
    namespace col
    {
        inline const juce::Colour ink{0xffe8e2cf};
        inline const juce::Colour inkDim{0xff8f897a};
        inline const juce::Colour rule{0xff2e2e31};
        inline const juce::Colour led{0xffff4638};
        inline const juce::Colour amber{0xffffb02e};
        inline const juce::Colour lcdInk{0xff1f2d0b};
    } // namespace col
} // namespace yrev::ui
