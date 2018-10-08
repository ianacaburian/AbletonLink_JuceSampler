/*
  ==============================================================================

    This file was auto-generated!

  ==============================================================================
*/

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "ableton/Link.hpp"
#define USE_TICKS_FOR_SEQUENCING 0
//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/

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
    void paint (Graphics& g) override;
    void resized() override;

private:
    struct EngineData
    {
        double requested_bpm;
        bool request_start;
        bool request_stop;
        double quantum;
        bool startstop_sync;
    };
    struct AbeSynth : Synthesiser
    {
        AbeSynth(const int);
    };
    EngineData pull_engine_data();
    ableton::Link::SessionState process_session_state(const EngineData&, const Micros&);
    void play_sequencer(const double, const int, const Micros, const double, 
                        const ableton::Link::SessionState&);
    void debug_state(const int, const Micros&, const double, const double, 
                     const double, const MidiMessage&);
    void timerCallback() override;
    void update_label();
    void showDeviceSetting();
    //==============================================================================
    std::unique_ptr<ableton::Link> link;
    EngineData shared_engine_data, lock_free_engine_data;    
    const int sampler_note = 60; // Temporary, more notes when more samples are added
    AbeSynth abe_synth;
    MidiBuffer mb;

    Value bpm, velocity;
    int64 buffer_sn, prev_beat_sn;
    Micros prev_sn_time;
    bool is_playing;
    int console_line_count, buffer_cycle_count;
    
    TextButton tb_settings, tb_link, tb_play, tb_stop, tb_sync;
    Slider sl_quantum, sl_bpm, sl_velocity;
    Label label;

    CriticalSection engine_data_guard;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
