#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "NoteExtractor.h"
#include "MidiExport.h"
#include "SoundModel.h"
#include <array>
#include <atomic>
#include <memory>

class Sample2MidiProcessor : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::Timer
{
public:
    Sample2MidiProcessor();
    ~Sample2MidiProcessor() override;

    //== AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "oraclee"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== Sample2MIDI API (message thread)
    bool loadSample (const juce::File& file, juce::String& error);
    void reanalyse();
    void play (double fromSeconds = 0.0);
    void stop();

    //== note editing (message thread)
    std::vector<mex::Note>& getNotesForEdit() { return notes; }
    void beginEdit();   // call before changing notes: takes an undo snapshot
    void endEdit();     // call after: marks the notes as retouched and refreshes playback
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    bool hasUserEdits() const { return userEdited; }
    bool importMidi (const juce::File&, int targetLayer);
    void previewNote (int pitch, int channel, int velocity); // short audition through the internal synth
    double getBeatSeconds() const; // length of one grid beat in the sample
    double getTuning() const { return tuning; }
    juce::String describeSound() const { return kit != nullptr ? kit->describe() : juce::String(); }
    bool isPlaying() const { return playing.load(); }

    // layer = -1 -> every enabled layer in one multi-track file
    bool exportMidi (const juce::File& dest, int layer = -1) const;
    juce::File writeTempMidi (int layer = -1) const;
    juce::Array<juce::File> exportAll (const juce::File& dest) const; // all-in-one + one file per instrument

    const std::vector<mex::Note>& getNotes() const { return notes; }
    int countNotes (int layer) const;
    std::shared_ptr<const juce::AudioBuffer<float>> getSample() const { return sample; }
    double getSampleLengthSeconds() const;
    juce::File getCurrentFile() const { return currentFile; }
    bool isAnalysing() const { return analysing.load(); }
    float getProgress() const { return progress.load(); }
    double getPlayPositionSeconds() const { return playPosSec.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }

    double getHostBpm() const { return hostBpm.load(); }
    double getSessionBpm() const;       // DAW tempo (or manual value)
    double getDetectedBpm() const { return detectedBpm; }
    double getSampleBpm() const;        // tempo the sample is read at
    bool isConforming() const;
    bool isLayerOn (int layer) const;
    mex::ExportOptions exportOptions (int layer = -1) const;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray& supportedExtensions();

private:
    void parameterChanged (const juce::String& id, float) override;
    void timerCallback() override;
    mex::Settings currentSettings() const;
    void rebuildPlaybackData();
    void allNotesOff (juce::MidiBuffer&, int offset);

    juce::AudioFormatManager formatManager;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };

    std::shared_ptr<const juce::AudioBuffer<float>> sample;
    double fileSampleRate = 44100.0;
    juce::File currentFile;

    // analysis results (message thread)
    std::vector<mex::Note> notes;
    double detectedBpm = 0.0, tuning = 0.0;
    std::vector<std::vector<mex::Note>> undoStack, redoStack;
    std::shared_ptr<mex::SoundKit> kit, pendingKit;      // message thread
    std::shared_ptr<const mex::SoundKit> kitAudio;       // read by the voices (atomic_load / atomic_store)
    bool userEdited = false;
    int editVersion = 0;
    std::vector<mex::Note> restoredNotes;
    bool hasRestoredNotes = false;
    juce::CriticalSection resultLock;
    mex::Result pendingResult;
    int pendingGeneration = -1;
    std::atomic<int> generation { 0 };
    std::atomic<bool> analysing { false }, paramsDirty { false };
    std::atomic<float> progress { 0.0f };
    juce::uint32 dirtySince = 0;
    juce::String playbackSignature;

    // playback (audio thread)
    struct PlayNote { double start, end; int pitch, channel, velocity; };
    juce::SpinLock audioLock;
    std::vector<PlayNote> previewNotes; // seconds, sample time
    std::vector<PlayNote> hostNotes;    // beats, session time
    double patternBeats = 16.0;
    std::shared_ptr<const juce::AudioBuffer<float>> playSample;
    double playSampleRate = 44100.0;
    std::atomic<bool> playing { false }, restartRequested { false }, stopRequested { false };
    double playPos = 0.0, hostSr = 44100.0, expectedPpq = -1.0;
    std::atomic<double> playPosSec { 0.0 }, hostBpm { 0.0 }, playFrom { 0.0 };
    struct PreviewEvent { int pitch, channel, velocity; };
    juce::AbstractFifo previewFifo { 64 };
    std::array<PreviewEvent, 64> previewEvents {};
    struct PendingOff { int pitch = -1, channel = 1, samplesLeft = 0; };
    std::array<PendingOff, 32> pendingOffs {};
    std::atomic<bool> hostPlaying { false };
    bool wasHostPlaying = false;
    std::array<std::array<bool, 128>, 17> activeOut {};
    juce::Synthesiser synth;

    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sample2MidiProcessor)
};
