#pragma once
#include "../JuceLibraryCode/JuceHeader.h"

#include <ableton/Link.hpp>
#include <ableton/link/HostTimeFilter.hpp>

//==============================================================================
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
    void calculate_output_time(const double sample_rate, const int buffer_size);
    EngineData pull_engine_data();
    void process_session_state(const EngineData& engine_data);
    void trigger_sampler(const double sample_rate, const double quantum, const int buffer_size);
    void show_audio_device_settings();
    static String double_str(const double d);
    
    //==============================================================================
    void paint(Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void update_label();

    //==============================================================================
    std::unique_ptr<ableton::Link> link;
    ableton::link::HostTimeFilter<ableton::link::platform::Clock> host_time_filter;
    std::unique_ptr<ableton::Link::SessionState> session;
    EngineData shared_engine_data, lock_free_engine_data;    
    std::mutex engine_data_guard;
    AbeSynth abe_synth;
    MidiBuffer midi_buffer;
    std::vector<double> beat_map;
    
    std::chrono::microseconds output_time;
    std::uint64_t sample_time = 0;
    bool is_playing = false;
    static constexpr double beat_length = 1.;
    static constexpr int middle_c = 60;
    
    
    // GUI
    TextButton tb_settings, tb_link, tb_play, tb_stop, tb_sync;
    Slider sl_quantum, sl_bpm, sl_velocity;
    Label main_display, lb_quantum, lb_bpm, lb_velocity;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
