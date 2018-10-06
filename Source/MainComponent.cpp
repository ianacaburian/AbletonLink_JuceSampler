/*
  ==============================================================================

    This file was auto-generated!

  ==============================================================================
*/

#include "MainComponent.h"
#include "ableton/link/HostTimeFilter.hpp"

ableton::link::HostTimeFilter<ableton::platforms::windows::Clock> host_time_filter;

//==============================================================================
MainComponent::MainComponent()
    : abe_synth{ sampler_note }
    , link_enabled{ var{ false } }
    , play{ var{ false } }
    , bpm{ 120. }
    , quantum{ 4 }
    , num_peers{ var{ 0 } }
    , velocity{ 0.2f }
    , tb_devices{ "devices" }
    , tb_link{ "link" }
    , tb_play{ "play" }    
{    
    // GUI init ===========================================================================
    for (auto* c : std::initializer_list<Component*>{ &tb_devices, &tb_link, &tb_play, &label, 
                                                      &sl_bpm, &sl_velocity })
        addAndMakeVisible(c);    
    setSize(600, 400);

    tb_devices.onClick = [this] { abe_synth.noteOn(1, 60, 1.f); };

    tb_link.setClickingTogglesState(true);
    tb_link.getToggleStateValue().referTo(link_enabled);
    tb_link.onClick = [this] { link->enable(tb_link.getToggleState()); };

    tb_play.setClickingTogglesState(true);
    tb_play.getToggleStateValue().referTo(play);

    label.setJustificationType(Justification::centred);
    
    sl_bpm.setSliderStyle(Slider::SliderStyle::LinearBar);
    sl_bpm.setRange(10, 200);
    sl_bpm.getValueObject().referTo(bpm);
    sl_bpm.onValueChange = [this]
    {
        const auto host_time = link->clock().micros();
        auto timeline = link->captureAppTimeline();
        timeline.requestBeatAtTime(0.0, host_time, quantum.getValue());
        timeline.setTempo(sl_bpm.getValue(), host_time);
        link->commitAudioTimeline(timeline);
    };
    sl_velocity.setSliderStyle(Slider::SliderStyle::LinearBar);
    sl_velocity.setRange(0, 1);
    sl_velocity.getValueObject().referTo(velocity);
    
    auto&& laf = getLookAndFeel();
    laf.setColour(TextButton::buttonColourId, Colours::black);
    laf.setColour(TextButton::buttonOnColourId, Colours::black);
    laf.setColour(TextButton::textColourOnId, Colours::limegreen);
    laf.setColour(Label::backgroundColourId, Colours::black);
    laf.setColour(Label::textColourId, Colours::white);

    startTimerHz(30);

    // Audio init =========================================================================
    setAudioChannels (0, 2);
    link.reset(new ableton::Link{ bpm.getValue() });
    link->enable(false);    
    link->setNumPeersCallback([this](std::size_t p) { num_peers = static_cast<int64>(p); });
    link->setTempoCallback([this](const double p) { bpm = p; });
}

MainComponent::~MainComponent()
{
    if (link->isEnabled())
        link->enable(false);
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRate)
{
    abe_synth.setCurrentPlaybackSampleRate(sampleRate);
}
void MainComponent::getNextAudioBlock (const AudioSourceChannelInfo& bufferToFill)
{
    if (!static_cast<bool>(play.getValue()))
        return;
    mb.clear();
    bufferToFill.clearActiveBufferRegion();
    const auto host_time = link->clock().micros();
    if (!deviceManager.getCurrentAudioDevice())
        return;

    const auto sample_rate = deviceManager.getCurrentAudioDevice()->getCurrentSampleRate();
    const auto device_latency = deviceManager.getCurrentAudioDevice()->getOutputLatencyInSamples();
    const auto device_buff_size = deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples();
    const auto output_latency = std::chrono::microseconds(llround(device_latency / sample_rate))
                              + std::chrono::microseconds(llround(1.0e6 * device_buff_size));

    const auto mb_start_ghost = host_time + output_latency; // ghost = global host
    
    auto timeline = link->captureAppTimeline();
    link->commitAppTimeline(timeline); // Timeline modifications are complete, commit the results
    
    const auto micros_per_sample = 1e6 / sample_rate;
    for (auto sn = 0; sn < bufferToFill.numSamples; ++sn)
    {
        const auto sn_ghost_time = mb_start_ghost + std::chrono::microseconds(llround(sn * micros_per_sample));
        const auto prev_micros_time = sn_ghost_time - std::chrono::microseconds(llround(micros_per_sample));
        const auto beat_time = timeline.beatAtTime(sn_ghost_time, quantum.getValue());
        const auto sn_ghost_phase_time = timeline.phaseAtTime(sn_ghost_time, 1);
        const auto prev_micros_phase_time = timeline.phaseAtTime(prev_micros_time, 1);

        if (0. <= beat_time && sn_ghost_phase_time < prev_micros_phase_time)
        {
            mb.addEvent(MidiMessage::noteOn(1, sampler_note, velocity.getValue()), sn);
            DBG("BEAT: " << beat_time << " PHASE: " << prev_micros_phase_time);

        }
    }
    abe_synth.renderNextBlock(*bufferToFill.buffer, mb, 0, bufferToFill.numSamples);
}
void MainComponent::releaseResources()
{}
//==============================================================================
void MainComponent::paint (Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}
void MainComponent::resized()
{
    auto&& bounds = getLocalBounds().toFloat();
    const auto width = bounds.getWidth();

    auto&& button_bounds = bounds.removeFromTop(bounds.getHeight() / 4);    
    auto&& buttons = std::initializer_list<Component*>{ &tb_devices, &tb_link, &tb_play };
    for (auto* b : buttons)
        b->setBounds(button_bounds.removeFromLeft(width / buttons.size()).toNearestIntEdges());
    
    label.setBounds(bounds.removeFromTop(bounds.getHeight() / 2).toNearestIntEdges());
    label.setFont(Font{ "consolas", "mono", label.getHeight() / 4.f });

    const auto sl_height = bounds.getHeight();
    auto&& sliders = std::initializer_list<Component*>{ &sl_bpm, &sl_velocity };
    for (auto* s : sliders)
        s->setBounds(bounds.removeFromTop(sl_height / sliders.size()).toNearestIntEdges());    
}
void MainComponent::timerCallback()
{
    update_label();
}
void MainComponent::update_label()
{
    const auto time = link->clock().micros();
    auto timeline = link->captureAppTimeline();
    const auto beat_str = String{ timeline.beatAtTime(time, quantum.getValue()), 3 }.paddedLeft(' ', 5);
    const auto phase_str = String{ timeline.phaseAtTime(time, quantum.getValue()), 3 }.paddedLeft(' ', 5);
    auto msg = String{};
    msg << "Peers: " << num_peers.toString() << "\n"
        << "Quantum: " << quantum.toString() << "\n"
        << "Beats: " << beat_str << "\n"
        << "Phase: " << phase_str << "\n";
    label.setText(msg, dontSendNotification);
}
void MainComponent::showDeviceSetting()
{
    AudioDeviceSelectorComponent selector(deviceManager,
        0, 256,
        0, 256,
        true, true,
        true, false);

    selector.setSize(400, 600);

    DialogWindow::LaunchOptions dialog;
    dialog.content.setNonOwned(&selector);
    dialog.dialogTitle = "Audio/MIDI Device Settings";
    dialog.componentToCentreAround = this;
    dialog.dialogBackgroundColour = getLookAndFeel().findColour(ResizableWindow::backgroundColourId);
    dialog.escapeKeyTriggersCloseButton = true;
    dialog.useNativeTitleBar = false;
    dialog.resizable = false;
    dialog.useBottomRightCornerResizer = false;

    dialog.runModal();
}
MainComponent::AbeSynth::AbeSynth(int sampler_note)
{
    auto samples_folder = File::getCurrentWorkingDirectory();    
    while (!samples_folder.isRoot())
    {
        samples_folder = samples_folder.getParentDirectory();
        if (samples_folder.getFileName() == "AbeLinkolnsJuce")
        {
            samples_folder = samples_folder.getChildFile("Samples");
            if (samples_folder.exists())
                break;
            else
                return;
        }
    }
    AudioFormatManager afm;
    afm.registerFormat(new WavAudioFormat(), true);    
    BigInteger note_range;
    note_range.setBit(sampler_note);
    auto add_sound = [this, &sampler_note, &afm, &note_range](const File& file)
    {
        if (file.existsAsFile())
        {
            auto* reader = afm.createReaderFor(file);
            addSound(new SamplerSound(String{}, *reader, note_range, sampler_note, 0.01, 0.1,
                                      reader->lengthInSamples / reader->sampleRate));
            reader->~AudioFormatReader();
        }
    };
    add_sound(samples_folder.getChildFile("Four.wav"));
    //add_sound(samples_folder.getChildFile("Scores.wav"));
    addVoice(new SamplerVoice{});
}
