// Renders the editor offscreen to a PNG:  s2m_snapshot <audio> <out.png>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    Sample2MidiProcessor proc;
    proc.prepareToPlay (44100, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    if (argc > 1)
    {
        juce::String err;
        proc.loadSample (juce::File (argv[1]), err);
        for (int i = 0; i < 100 && proc.isAnalysing(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File out (argc > 2 ? argv[2] : "snapshot.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    ed.reset();
    return 0;
}
