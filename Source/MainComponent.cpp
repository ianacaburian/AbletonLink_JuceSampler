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
    : shared_engine_data({ 0., false, false, 4., false })
    , lock_free_engine_data{ shared_engine_data }
    , abe_synth{ sampler_note }
    , bpm{ 120. }
    , velocity{ 0.2f }
    , is_playing{ false }
    , tb_devices{ "devices" }
    , tb_link{ "link" }
    , tb_play{ "play" }
    , tb_stop{ "stop" }
    , tb_sync{ "sync" }
{    
    // GUI initialization
    for (auto* c : std::initializer_list<Component*>{ &tb_devices, &tb_link, &tb_play, &tb_stop, &tb_sync, 
                                                      &label, &sl_bpm, &sl_velocity })
        addAndMakeVisible(c);    
    setSize(600, 400);
    startTimerHz(30);

    // Component callbacks
    tb_devices.onClick = [this] { abe_synth.noteOn(1, 60, 1.f); };
    tb_link.onClick = [this] { link->enable(tb_link.getToggleState()); };
    tb_play.onClick = [this] { const ScopedLock lock(engine_data_guard); 
                               shared_engine_data.request_start = true; };
    tb_stop.onClick = [this] { const ScopedLock lock(engine_data_guard); 
                               shared_engine_data.request_stop = true; };
    sl_bpm.onValueChange = [this] { const ScopedLock lock(engine_data_guard); 
                                    shared_engine_data.requested_bpm = sl_bpm.getValue(); };

    // Component properties
    tb_link.setClickingTogglesState(true);
    sl_bpm.setRange(10, 200);
    sl_bpm.getValueObject().referTo(bpm);
    sl_velocity.setRange(0, 1);
    sl_velocity.getValueObject().referTo(velocity);
    
    // Component appearance
    auto&& laf = getLookAndFeel();
    laf.setColour(TextButton::buttonColourId, Colours::black);
    laf.setColour(TextButton::buttonOnColourId, Colours::black);
    laf.setColour(TextButton::textColourOnId, Colours::limegreen);
    laf.setColour(Label::backgroundColourId, Colours::black);
    laf.setColour(Label::textColourId, Colours::white);    
    label.setJustificationType(Justification::right);
    for (auto* s : { &sl_bpm, &sl_velocity }) { s->setSliderStyle(Slider::SliderStyle::LinearBar);
                                                s->setTextBoxIsEditable(false); }

    // Audio initialization
    setAudioChannels (0, 2);
    link.reset(new ableton::Link{ bpm.getValue() });    
    link->setTempoCallback([this](const double p) { bpm = p; });
    link->enable(false);
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
    auto* current_device = deviceManager.getCurrentAudioDevice();
    if (!current_device || !link)
        return;

    const auto sample_rate = current_device->getCurrentSampleRate();
    auto output_host_time = link->clock().micros();
    output_host_time += calculate_latency_time(current_device, sample_rate, bufferToFill.numSamples);

    const auto engine_data = pull_engine_data();
    mb.clear();
    bufferToFill.clearActiveBufferRegion();

    auto session = process_session_state(engine_data, output_host_time);

    if (is_playing)
        play_sequencer(sample_rate, bufferToFill.numSamples, output_host_time, 
                       engine_data.quantum, session);
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
ableton::Link::SessionState MainComponent::process_session_state(
    const EngineData& engine_data, 
    const std::chrono::microseconds& output_host_time)
{
    auto session = link->captureAudioSessionState();

    if (engine_data.request_start)
        session.setIsPlaying(true, output_host_time);

    if (engine_data.request_stop)
        session.setIsPlaying(false, output_host_time);

    if (!is_playing && session.isPlaying())
    {   // Reset the timeline so that beat 0 corresponds to the time when transport starts
        session.requestBeatAtStartPlayingTime(0, engine_data.quantum);
        is_playing = true;
    }
    else if (is_playing && !session.isPlaying())
        is_playing = false;

    if (engine_data.requested_bpm > 0) // Set the newly requested tempo from the beginning of this buffer.
        session.setTempo(engine_data.requested_bpm, output_host_time);

    link->commitAudioSessionState(session); // Timeline modifications are complete, commit the results
    return session;
}
void MainComponent::play_sequencer(
    const double sample_rate, const int num_samples, const std::chrono::microseconds& time, 
    const double quantum, const ableton::Link::SessionState& session)
{
    using namespace std::chrono;
    const auto micros_per_sample = 1e6 / sample_rate; // number of microseconds that elapse between samples 
    for (auto sn = 0; sn != num_samples; ++sn) // sn = sample no.
    {
        const auto sn_time = time + microseconds{ llround(sn * micros_per_sample) };
        const auto prev_sn_time = sn_time - microseconds{ llround(micros_per_sample) };

        const auto beat = session.beatAtTime(sn_time, quantum);
        const auto phase = session.phaseAtTime(sn_time, 1);
        const auto prev_sn_phase = session.phaseAtTime(prev_sn_time, 1);

        const auto is_count_in = beat < 0.; // This beat occurs before the bar begins (anacrusis).
        const auto is_downbeat = phase < prev_sn_phase; // The phase has wrapped since last sample.

        if (!is_count_in && is_downbeat)
            trigger_sample(sn);
    }
}
std::chrono::microseconds MainComponent::calculate_latency_time(AudioIODevice* device, const double sample_rate, const int num_samples)
{   // It is vital to ensure the audio buffer uses the host_time that the user actually hears (time at speaker)
    using namespace std::chrono;
    auto output_latency = microseconds{ llround(device->getOutputLatencyInSamples() / sample_rate) };
    const double buffer_size = device->getCurrentBufferSizeSamples() / sample_rate;
    output_latency += std::chrono::microseconds{ llround(1.0e6 * buffer_size) };
    return output_latency; 
}
MainComponent::EngineData MainComponent::pull_engine_data()
{
    auto engine_data = EngineData{};
    if (engine_data_guard.tryEnter())
    {
        engine_data.requested_bpm = shared_engine_data.requested_bpm;
        shared_engine_data.requested_bpm = 0;

        engine_data.request_start = shared_engine_data.request_start;
        shared_engine_data.request_start = false;

        engine_data.request_stop = shared_engine_data.request_stop;
        shared_engine_data.request_stop = false;

        lock_free_engine_data.quantum = shared_engine_data.quantum;
        lock_free_engine_data.startstop_sync_on = shared_engine_data.startstop_sync_on;

        engine_data_guard.exit();
    }
    engine_data.quantum = lock_free_engine_data.quantum;
    return engine_data;
}
void MainComponent::trigger_sample(const int sn)
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
    const auto beat = session.beatAtTime(time, shared_engine_data.quantum);
    const auto phase = session.phaseAtTime(time, shared_engine_data.quantum);    
    label.setText(String{ "Peers: " + String{ link->numPeers() }
                        + "\nState: " + (beat < 0. ? "count-in.." : session.isPlaying() ? "playing" : "stopped")
                        + "\nBeats: " + String{ beat, 3 }.paddedLeft(' ', 7)
                        + "\nPhase: " + String{ phase, 3 }.paddedLeft(' ', 7) }, dontSendNotification);
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
MainComponent::AbeSynth::AbeSynth(const int sampler_note)
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
