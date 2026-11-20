// Renders the extracted MIDI with the adaptive synth and with the basic one: s2m_synthtest <audio> <outdir>
#include "../Source/PluginProcessor.h"
#include <iostream>
int main (int, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    Sample2MidiProcessor proc;
    proc.prepareToPlay (44100, 512);
    juce::String err;
    proc.loadSample (juce::File (argv[1]), err);
    for (int i = 0; i < 300 && proc.isAnalysing(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    std::cout << "kit: " << proc.describeSound() << "\n";
    for (int adaptive = 0; adaptive < 2; ++adaptive)
    {
        proc.apvts.getParameter ("adaptive")->setValueNotifyingHost ((float) adaptive);
        proc.apvts.getParameter ("loop")->setValueNotifyingHost (0.0f);
        proc.play (0.0);
        const int total = (int) (proc.getSampleLengthSeconds() * 44100);
        juce::AudioBuffer<float> out (1, total), blk (2, 512);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < total; pos += 512)
        {
            proc.processBlock (blk, midi);
            out.copyFrom (0, pos, blk, 0, 0, std::min (512, total - pos));
        }
        proc.stop();
        juce::MidiBuffer m2; proc.processBlock (blk, m2);
        juce::File f (juce::String (argv[2]) + (adaptive ? "/adaptive.wav" : "/basic.wav"));
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 44100, 1, 16, {}, 0));
        w->writeFromAudioSampleBuffer (out, 0, total);
    }
    return 0;
}
