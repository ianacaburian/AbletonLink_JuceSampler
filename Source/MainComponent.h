/*
  ==============================================================================

    This file was auto-generated!

  ==============================================================================
*/

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "ableton/Link.hpp"

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/

class MainComponent   : public AudioAppComponent, public Timer
{
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
        bool startstop_sync_on;
    };
    struct AbeSynth : Synthesiser
    {
        AbeSynth(const int);
    };
    std::chrono::microseconds calculate_latency_time(AudioIODevice*, const double, const int);
    EngineData pull_engine_data();
    ableton::Link::SessionState process_session_state(const EngineData&, const std::chrono::microseconds&);
    void play_sequencer(const double, const int, const std::chrono::microseconds&, 
                        const double, const ableton::Link::SessionState&);
    void trigger_sample(const int);
    void timerCallback() override;
    void update_label();
    void showDeviceSetting();
    //==============================================================================
    std::unique_ptr<ableton::Link> link;
    EngineData shared_engine_data, lock_free_engine_data;    
    const int sampler_note = 60;
    AbeSynth abe_synth;
    MidiBuffer mb;

    Value bpm, velocity;
    bool is_playing;
    
    TextButton tb_devices, tb_link, tb_play, tb_stop, tb_sync;
    Slider sl_bpm, sl_velocity;
    Label label;

    CriticalSection engine_data_guard;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
