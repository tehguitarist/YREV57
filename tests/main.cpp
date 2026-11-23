#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui; // the message thread, for anything that needs it
    return Catch::Session ().run (argc, argv);
}
