// Renders any installed VST3/AU plugin editor to a PNG:  s2m_peek <plugin path or AU id> <out.png>
#include <juce_audio_processors/juce_audio_processors.h>
#import <Cocoa/Cocoa.h>
#include <iostream>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::String path (argv[1]);
    std::unique_ptr<juce::AudioPluginFormat> fmt;
    if (path.endsWith (".vst3")) fmt = std::make_unique<juce::VST3PluginFormat>();
    else fmt = std::make_unique<juce::AudioUnitPluginFormat>();
    juce::OwnedArray<juce::PluginDescription> found;
    fmt->findAllTypesForFile (found, path);
    if (found.isEmpty()) { std::cout << "not found\n"; return 1; }
    std::cout << found[0]->name << " " << found[0]->version << " by " << found[0]->manufacturerName << "\n";
    juce::String err;
    auto inst = fmt->createInstanceFromDescription (*found[0], 48000, 512, err);
    if (! inst) { std::cout << err << "\n"; return 1; }
    inst->prepareToPlay (48000, 512);
    std::cout << "params:";
    for (auto* p : inst->getParameters()) std::cout << " [" << p->getName (40) << "=" << p->getCurrentValueAsText() << "]";
    std::cout << "\n";
    auto* ed = inst->createEditorIfNeeded();
    if (! ed) { std::cout << "no editor\n"; return 1; }
    juce::DocumentWindow w ("peek", juce::Colours::black, 0);
    w.setUsingNativeTitleBar (true);
    w.setContentNonOwned (ed, true);
    w.setTopLeftPosition (40, 40);
    w.setVisible (true);
    for (int i = 0; i < 40; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    NSView* v = (NSView*) w.getPeer()->getNativeHandle();
    NSRect r = [v bounds];
    NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect: r];
    [v cacheDisplayInRect: r toBitmapImageRep: rep];
    NSData* png = [rep representationUsingType: NSBitmapImageFileTypePNG properties: @{}];
    [png writeToFile: [NSString stringWithUTF8String: argv[2]] atomically: YES];
    std::cout << "editor " << ed->getWidth() << "x" << ed->getHeight() << " saved\n";
    w.clearContentComponent();
    delete ed;
    inst.reset();
    return 0;
}
