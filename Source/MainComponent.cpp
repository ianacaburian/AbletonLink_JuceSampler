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
    , queing{ var{ false } }
    , stopping{ var{ false } }
    , bpm{ 120. }
    , quantum{ 4 }
    , num_peers{ var{ 0 } }
    , velocity{ 0.2f }
    , tb_devices{ "devices" }
    , tb_link{ "link" }
    , tb_play{ "play" }
    , tb_stop{ "stop" }
{    
    // GUI init ===========================================================================
    for (auto* c : std::initializer_list<Component*>{ &tb_devices, &tb_link, &tb_play, &tb_stop, 
                                                      &label, &sl_bpm, &sl_velocity })
        addAndMakeVisible(c);    
    setSize(600, 400);

    tb_devices.onClick = [this] { abe_synth.noteOn(1, 60, 1.f); };

    tb_link.setClickingTogglesState(true);
    tb_link.getToggleStateValue().referTo(link_enabled);
    tb_link.onClick = [this] { link->enable(tb_link.getToggleState()); };
        
    tb_play.onClick = [this] { queing = true; };
    tb_stop.onClick = [this] { stopping = true; };

    label.setJustificationType(Justification::right);
    
    for (auto* s : { &sl_bpm, &sl_velocity })
    {
        s->setSliderStyle(Slider::SliderStyle::LinearBar);
        s->setTextBoxIsEditable(false);
    }
    sl_bpm.setRange(10, 200);
    sl_bpm.getValueObject().referTo(bpm);
    sl_bpm.onValueChange = [this]
    {
        const auto host_time = link->clock().micros();
        auto timeline = link->captureAppSessionState();
        timeline.requestBeatAtTime(0.0, host_time, quantum.getValue());
        timeline.setTempo(sl_bpm.getValue(), host_time);
        link->commitAppSessionState(timeline);
    };
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
    mb.clear();
    bufferToFill.clearActiveBufferRegion();
    
    auto* current_device = deviceManager.getCurrentAudioDevice();
    if (!current_device || !link)
        return;

    auto host_time = link->clock().micros();
    const auto sample_rate = current_device->getCurrentSampleRate();
    const auto latency_hz = current_device->getOutputLatencyInSamples() / sample_rate;    
    const auto latency_time = std::chrono::microseconds(llround(latency_hz));
    auto mb_start_ghost = host_time; // mb = midi buffer, ghost = global host
    mb_start_ghost += latency_time;
    auto mb_end_ghost = mb_start_ghost; 
    mb_end_ghost += std::chrono::microseconds(llround(1.0e6 * bufferToFill.numSamples));
    auto session = link->captureAudioSessionState();

    if (queing.getValue())
    {
        session.setIsPlayingAndRequestBeatAtTime(true, mb_start_ghost, 0., quantum.getValue());
        DBG("isplaying:" << (int)session.isPlaying());
        queing = false;
    }
    if (stopping.getValue())
    {
        const auto end_of_bar = session.timeAtBeat(4., quantum.getValue());
        
        session.setIsPlaying(false, end_of_bar);        
        DBG("isplaying:" << (int)session.isPlaying());

        //session.setIsPlaying(false, mb_start_ghost);
        stopping = false;
    }
    link->commitAudioSessionState(session); // Timeline modifications are complete, commit the results

    //if (session.timeForIsPlaying() < session.isPlaying())
    //{
        const auto micros_per_sample = 1e6 / sample_rate;
        for (auto sn = 0; sn < bufferToFill.numSamples; ++sn)
        {
            const auto sn_ghost_time = mb_start_ghost + std::chrono::microseconds(llround(sn * micros_per_sample));
            if (session.timeForIsPlaying() < sn_ghost_time && !session.isPlaying())
                continue;
            const auto beat = session.beatAtTime(sn_ghost_time, quantum.getValue());
            const auto awaiting_first_beat = beat < 0.;

            const auto phase = session.phaseAtTime(sn_ghost_time, 1);
            const auto prev_micros_time = sn_ghost_time - std::chrono::microseconds(llround(micros_per_sample));
            const auto prev_micros_phase = session.phaseAtTime(prev_micros_time, 1);
            const auto is_downbeat = phase < prev_micros_phase;

            if (!awaiting_first_beat && is_downbeat)
                play_sample(sn);
        }
    //}
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
    auto&& buttons = std::initializer_list<Component*>{ &tb_devices, &tb_link, &tb_play, &tb_stop };
    for (auto* b : buttons)
        b->setBounds(button_bounds.removeFromLeft(width / buttons.size()).toNearestIntEdges());
        
    label.setBounds(bounds.removeFromTop(bounds.getHeight() / 2).toNearestIntEdges().withRight(tb_link.getRight()));
    label.setFont(Font{ "consolas", "mono", label.getHeight() / 4.f });

    const auto sl_height = bounds.getHeight();
    auto&& sliders = std::initializer_list<Component*>{ &sl_bpm, &sl_velocity };
    for (auto* s : sliders)
        s->setBounds(bounds.removeFromTop(sl_height / sliders.size()).toNearestIntEdges());    
}
void MainComponent::play_sample(int sn)
{
    mb.addEvent(MidiMessage::noteOn(1, sampler_note, velocity.getValue()), sn);
}
void MainComponent::timerCallback()
{
    update_label();
}
void MainComponent::update_label()
{
    const auto time = link->clock().micros();
    const auto session = link->captureAppSessionState();    
    const auto state_str = queing.getValue() ? "queueing.." : session.isPlaying() ? "playing" : stopping.getValue() ? "stopping" : "stopped";
    const auto beat_str = String{ session.beatAtTime(time, quantum.getValue()), 3 }.paddedLeft(' ', 7);
    const auto phase_str = String{ session.phaseAtTime(time, quantum.getValue()), 3 }.paddedLeft(' ', 7);
    label.setText(String{ "Peers: " + num_peers.toString() + "\n"
                        + "State: " + state_str + "\n"
                        + "Beats: " + beat_str + "\n"
                        + "Phase: " + phase_str + "\n" }, dontSendNotification);
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
