// Simulates a DAW at 120 BPM playing 2 bars and prints the plugin's MIDI output (first events)
#include "../Source/PluginProcessor.h"
#include <iostream>
struct FakeHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (true); return p;
    }
};
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    Sample2MidiProcessor proc;
    FakeHead head;
    proc.setPlayHead (&head);
    proc.prepareToPlay (48000, 512);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    proc.processBlock (buf, midi); // let it learn the host tempo
    juce::String err;
    proc.loadSample (juce::File (argv[1]), err);
    for (int i = 0; i < 200 && proc.isAnalysing(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    std::cout << "session " << proc.getSessionBpm() << " sample " << proc.getSampleBpm() << "\n";
    int ons[17] {};
    double firstBeat[17]; for (auto& f : firstBeat) f = -1;
    for (int b = 0; b < 48000 * 8 / 512; ++b)
    {
        proc.processBlock (buf, midi);
        for (const auto m : midi)
        {
            auto msg = m.getMessage();
            if (! msg.isNoteOn()) continue;
            const double beat = head.ppq + m.samplePosition * head.bpm / 60.0 / 48000.0;
            int ch = msg.getChannel(); ons[ch]++;
            if (ons[ch] <= 4) std::cout << "ch" << ch << " note " << msg.getNoteNumber() << " vel " << (int) msg.getVelocity() << " @beat " << juce::String (beat, 3) << "\n";
        }
        head.ppq += 512 * head.bpm / 60.0 / 48000.0;
    }
    std::cout << "16 beats @120: notes on ch1=" << ons[1] << " ch2=" << ons[2] << " ch3=" << ons[3] << " ch10=" << ons[10] << "\n";
    proc.setPlayHead (nullptr);
}
