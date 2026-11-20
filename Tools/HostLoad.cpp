// Loads the INSTALLED oraclee plugins (VST3 + AU) like a DAW would and exercises them.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <CoreFoundation/CoreFoundation.h>

struct Head : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120; bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    { PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing); return p; }
};

int test (juce::AudioPluginFormat& fmt, const juce::String& id, const juce::File& sample)
{
    juce::OwnedArray<juce::PluginDescription> found;
    fmt.findAllTypesForFile (found, id);
    if (found.isEmpty()) { std::cout << fmt.getName() << ": NOT FOUND " << id << "\n"; return 1; }
    auto d = *found[0];
    std::cout << fmt.getName() << ": " << d.name << " by " << d.manufacturerName << "  instrument=" << d.isInstrument << "\n";
    juce::String err;
    auto inst = fmt.createInstanceFromDescription (d, 48000, 512, err);
    if (inst == nullptr) { std::cout << "  create failed: " << err << "\n"; return 1; }
    Head head;
    inst->setPlayHead (&head);
    inst->enableAllBuses();
    inst->prepareToPlay (48000, 512);
    std::cout << "  produces MIDI: " << inst->producesMidi() << ", params: " << inst->getParameters().size() << "\n";

    // give it a sample through its saved state (the same path a DAW project reload takes).
    // The host wraps the plugin's own blob: VST3 -> <VST3PluginState><IComponent>base64</IComponent>,
    // AU -> XML plist with <key>jucePluginState</key><data>base64</data>.
    auto setSample = [&] (juce::MemoryBlock& inner) -> bool
    {
        auto x = juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
        if (x == nullptr) return false;
        const auto sz = (size_t) juce::ByteOrder::littleEndianInt (static_cast<const char*> (inner.getData()) + 4) + 8;
        juce::MemoryBlock tail;
        if (inner.getSize() > sz) tail.append (static_cast<const char*> (inner.getData()) + sz, inner.getSize() - sz);
        x->setAttribute ("file", sample.getFullPathName());
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (*x, out);
        out.append (tail.getData(), tail.getSize());
        inner = out;
        return true;
    };
    juce::MemoryBlock st;
    inst->getStateInformation (st);
    bool ok = false;
    if (auto outer = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize())) // VST3 host wrapper
    {
        if (auto* ic = outer->getChildByName ("IComponent"))
        {
            juce::MemoryBlock inner;
            inner.fromBase64Encoding (ic->getAllSubText().trim());
            if (setSample (inner))
            {
                ic->deleteAllTextElements();
                ic->addTextElement (inner.toBase64Encoding());
                juce::MemoryBlock nb;
                juce::AudioProcessor::copyXmlToBinary (*outer, nb);
                inst->setStateInformation (nb.getData(), (int) nb.getSize());
                ok = true;
            }
        }
    }
    else // AU host wrapper: binary plist with a "jucePluginState" data entry
    {
        CFDataRef data = CFDataCreate (nullptr, (const UInt8*) st.getData(), (CFIndex) st.getSize());
        auto plist = (CFMutableDictionaryRef) CFPropertyListCreateWithData (nullptr, data, kCFPropertyListMutableContainersAndLeaves, nullptr, nullptr);
        if (plist != nullptr && CFGetTypeID (plist) == CFDictionaryGetTypeID())
        {
            auto key = CFSTR ("jucePluginState");
            if (auto js = (CFDataRef) CFDictionaryGetValue (plist, key))
            {
                juce::MemoryBlock inner (CFDataGetBytePtr (js), (size_t) CFDataGetLength (js));
                if (setSample (inner))
                {
                    CFDataRef nd = CFDataCreate (nullptr, (const UInt8*) inner.getData(), (CFIndex) inner.getSize());
                    CFDictionarySetValue (plist, key, nd);
                    CFRelease (nd);
                    CFDataRef outData = CFPropertyListCreateData (nullptr, plist, kCFPropertyListBinaryFormat_v1_0, 0, nullptr);
                    inst->setStateInformation (CFDataGetBytePtr (outData), (int) CFDataGetLength (outData));
                    CFRelease (outData);
                    ok = true;
                }
            }
        }
        if (plist) CFRelease (plist);
        CFRelease (data);
    }
    std::cout << "  state with sample injected: " << ok << "\n";

    std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
    std::cout << "  editor: " << (ed ? juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()) : juce::String ("none")) << "\n";

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    // let the analysis run (message loop) while the audio thread keeps calling processBlock
    for (int i = 0; i < 120; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        midi.clear(); inst->processBlock (buf, midi);
    }
    for (auto* prm : inst->getParameters())   // "Jouer avec le DAW" is off by default
        if (prm->getName (64) == "Jouer avec le DAW") prm->setValueNotifyingHost (1.0f);
    head.playing = true;
    int on[17] {};
    for (int b = 0; b < 48000 * 8 / 512; ++b)
    {
        midi.clear();
        inst->processBlock (buf, midi);
        for (const auto m : midi) if (m.getMessage().isNoteOn()) ++on[m.getMessage().getChannel()];
        head.ppq += 512 * head.bpm / 60.0 / 48000.0;
        if ((b & 15) == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
    std::cout << "  8 s of DAW playback -> note-ons ch1=" << on[1] << " ch2=" << on[2] << " ch3=" << on[3] << " ch10=" << on[10] << "\n";
    ed.reset();
    inst->releaseResources();
    inst.reset();
    return (on[1] + on[2] + on[3] + on[10]) > 0 ? 0 : 1;
}

int main (int, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File sample (argv[1]);
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    int fails = 0;
    juce::VST3PluginFormat vst3;
    fails += test (vst3, home.getChildFile ("Library/Audio/Plug-Ins/VST3/oraclee.vst3").getFullPathName(), sample);
    juce::AudioUnitPluginFormat au;
    juce::StringArray aus;
    for (auto& s : au.searchPathsForPlugins ({}, true, false))
        if (s.containsIgnoreCase ("Orc1")) aus.add (s);
    if (aus.isEmpty()) { std::cout << "AU: not registered\n"; ++fails; }
    else fails += test (au, aus[0], sample);
    std::cout << (fails ? "HOST TEST FAILED" : "HOST TEST PASSED") << "\n";
    return fails;
}
