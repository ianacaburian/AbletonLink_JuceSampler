#pragma once
#include "../JuceLibraryCode/JuceHeader.h"

#include <ableton/Link.hpp>
#include <ableton/link/HostTimeFilter.hpp>

#define USE_BEATS_FOR_SEQUENCING 1 // Select which "play_sequencer" method to use (see getNextAudioBlock())
//==============================================================================
class MainComponent   : public AudioAppComponent, public Timer
{
    using Micros = std::chrono::microseconds;
public:
    //==============================================================================
    MainComponent();
    ~MainComponent();

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    //==============================================================================

private:
    struct EngineData
    {
        double requested_bpm;
        bool request_start;
        bool request_stop;
        double quantum;
        bool startstop_sync;
        JUCE_LEAK_DETECTOR(EngineData)
    };
    struct AbeSynth : Synthesiser
    {
        AbeSynth(const int);
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AbeSynth)
    };
    EngineData pull_engine_data();
    ableton::Link::SessionState process_session_state(const EngineData&, const Micros&);
    void play_sequencer_beats(const double, const int, const Micros, const ableton::Link::SessionState&);
    void play_sequencer_phase(const double, const int, const Micros, const double, const ableton::Link::SessionState&);
    void debug_state(const bool, const int, const Micros&, const double, const MidiMessage&, 
        const Micros& = Micros{}, const double = 0., const double = 0.);
    void show_audio_device_settings();
    
    //==============================================================================
    void paint(Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void update_label();

    //==============================================================================
    std::unique_ptr<ableton::Link> link;
    EngineData shared_engine_data, lock_free_engine_data;    
    std::mutex engine_data_guard;
    const int sampler_note = 60; // Temporary, more notes when more samples are added
    AbeSynth abe_synth;
    MidiBuffer mb;    
    Value velocity;
    bool is_playing;
    
    // GUI
    TextButton tb_settings, tb_link, tb_play, tb_stop, tb_sync;
    Slider sl_quantum, sl_bpm, sl_velocity;
    Label main_display, lb_quantum, lb_bpm, lb_velocity;    

    // Debug-only
    int64 buffer_sn, prev_beat_sn;
    int console_line_count, buffer_cycle_count;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
