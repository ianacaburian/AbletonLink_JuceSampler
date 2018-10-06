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
    void timerCallback() override;
    void update_label();
    void showDeviceSetting();
    //==============================================================================
    struct AbeSynth : Synthesiser
    {
        AbeSynth(int);
    };
    std::unique_ptr<ableton::Link> link;
    const int sampler_note = 60;
    AbeSynth abe_synth;
    MidiBuffer mb;

    Value link_enabled, play, bpm, quantum, num_peers, velocity;
    
    
    TextButton tb_devices, tb_link, tb_play;
    Slider sl_bpm, sl_velocity;
    Label label;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
