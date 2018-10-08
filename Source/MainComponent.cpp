/*
  ==============================================================================

    This file was auto-generated!

  ==============================================================================
*/

#include "MainComponent.h"
#include "ableton/link/HostTimeFilter.hpp"

//ableton::link::HostTimeFilter<ableton::platforms::windows::Clock> host_time_filter;

//==============================================================================
MainComponent::MainComponent()
    : shared_engine_data({ 0., false, false, 4., false })
    , lock_free_engine_data{ shared_engine_data }
    , abe_synth{ sampler_note }
    , velocity{ 0.2f }
    , buffer_sn{ 0 }
    , prev_beat_sn{ 0 }
    , prev_sn_time{ 0 }
    , is_playing{ false }
    , console_line_count{ 0 }
    , tb_settings{ "Audio Settings" }
    , tb_link{ "Link" }
    , tb_play{ "Play" }
    , tb_stop{ "Stop" }
    , tb_sync{ "Start Stop Sync" }
{    
    // GUI initialization
    for (auto* c : std::initializer_list<Component*>{ &tb_settings, &tb_link, &tb_play, &tb_stop, &tb_sync, 
                                                      &label, &sl_quantum, &sl_bpm, &sl_velocity })
        addAndMakeVisible(c);    
    setSize(600, 400);
    startTimerHz(30);

    // Component callbacks
    tb_settings.onClick = [this] { abe_synth.noteOn(1, 60, 1.f); };
    tb_link.onClick = [this] { link->enable(tb_link.getToggleState()); };
    tb_play.onClick = [this] { const ScopedLock lock(engine_data_guard); 
                               shared_engine_data.request_start = true; };
    tb_stop.onClick = [this] { const ScopedLock lock(engine_data_guard); 
                               shared_engine_data.request_stop = true; };
    tb_sync.onClick = [this] { link->enableStartStopSync(tb_sync.getToggleState()); };
    sl_quantum.onValueChange = [this] { const ScopedLock lock(engine_data_guard); 
                                        shared_engine_data.quantum = sl_quantum.getValue(); };
    sl_bpm.onValueChange = [this] { const ScopedLock lock(engine_data_guard); 
                                    shared_engine_data.requested_bpm = sl_bpm.getValue(); };

    // Component initialization
    tb_link.setClickingTogglesState(true);
    tb_sync.setClickingTogglesState(true);
    sl_quantum.setRange(1., 8., 1.);
    sl_quantum.getValueObject() = shared_engine_data.quantum;
    sl_bpm.setRange(10., 200.);
    sl_bpm.getValueObject() = 120.;
    sl_velocity.setRange(0., 1.);
    sl_velocity.getValueObject().referTo(velocity);
    
    // Component appearance
    auto&& laf = getLookAndFeel();
    laf.setColour(TextButton::buttonColourId, Colours::black);
    laf.setColour(TextButton::buttonOnColourId, Colours::black);
    laf.setColour(TextButton::textColourOnId, Colours::limegreen);
    laf.setColour(Label::backgroundColourId, Colours::black);
    laf.setColour(Label::textColourId, Colours::white);    
    label.setJustificationType(Justification::right);
    for (auto* s : { &sl_quantum, &sl_bpm, &sl_velocity }) { s->setSliderStyle(Slider::SliderStyle::LinearBar);
                                                             s->setTextBoxIsEditable(false); }

    // Audio initialization
    setAudioChannels (0, 2);
    link.reset(new ableton::Link{ sl_bpm.getValue() });
    link->setTempoCallback([this](const double p) { sl_bpm.getValueObject() = p; });
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
    const auto micros_per_sample = 1e6 / sample_rate; // number of microseconds that elapse between samples 

    //auto output_host_time = host_time_filter.sampleTimeToHostTime(buffer_sn);
    auto output_host_time = link->clock().micros();
    output_host_time += Micros{ llround(micros_per_sample * current_device->getOutputLatencyInSamples()) };

    const auto engine_data = pull_engine_data();
    mb.clear();
    bufferToFill.clearActiveBufferRegion();

    auto session = process_session_state(engine_data, output_host_time);

    if (is_playing)
        play_sequencer(micros_per_sample, bufferToFill.numSamples, output_host_time,
                              engine_data.quantum, session);
    abe_synth.renderNextBlock(*bufferToFill.buffer, mb, 0, bufferToFill.numSamples);

    // Update "debug-only" vars
    buffer_sn += bufferToFill.numSamples;
    ++buffer_cycle_count;
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
    auto&& buttons = std::initializer_list<Component*>{ &tb_settings, &tb_link, &tb_play, &tb_stop, &tb_sync };
    for (auto* b : buttons)
        b->setBounds(button_bounds.removeFromLeft(width / buttons.size()).toNearestIntEdges());
        
    label.setBounds(bounds.removeFromTop(bounds.getHeight() / 2).toNearestIntEdges().withRight(tb_stop.getRight()));
    label.setFont(Font{ "consolas", "mono", label.getHeight() / 4.f });

    const auto sl_height = bounds.getHeight();
    auto&& sliders = std::initializer_list<Component*>{ &sl_quantum, &sl_bpm, &sl_velocity };
    for (auto* s : sliders)
        s->setBounds(bounds.removeFromTop(sl_height / sliders.size()).toNearestIntEdges());    
}
ableton::Link::SessionState MainComponent::process_session_state(
    const EngineData& engine_data, 
    const Micros& output_host_time)
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
    const double micros_per_sample, const int num_samples, const Micros time,
    const double quantum, const ableton::Link::SessionState& session)
{ 

    for (auto sn = 0; sn != num_samples; ++sn) // sn = sample no.
    {
        const auto sn_time = time + Micros{ llround(sn * micros_per_sample) };
        prev_sn_time = sn_time - Micros{ llround(micros_per_sample) };

        const auto beat = session.beatAtTime(sn_time, quantum);
        const auto phase = session.phaseAtTime(sn_time, 1);
        const auto prev_sn_phase = session.phaseAtTime(prev_sn_time, 1);

        const auto is_count_in = beat < 0.; // This beat occurs before the bar begins (anacrusis).
        const auto is_downbeat = phase < prev_sn_phase; // The phase has wrapped since last sample.

        if (!is_count_in && is_downbeat) // trigger sample
        {
            const auto midi_msg = MidiMessage::noteOn(1, sampler_note, velocity.getValue());
            mb.addEvent(midi_msg, sn);
            debug_state(sn, sn_time, beat, phase, prev_sn_phase, midi_msg);
        }
    }
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
        lock_free_engine_data.startstop_sync = shared_engine_data.startstop_sync;

        engine_data_guard.exit();
    }
    engine_data.quantum = lock_free_engine_data.quantum;
    return engine_data;
}
void MainComponent::debug_state(const int sn, const Micros& sn_time, const double beat, 
                                const double phase, const double prev_sn_phase, const MidiMessage& midi_msg)
{    
    if (beat < 1.)
    {
        prev_beat_sn = buffer_cycle_count = 0;
        buffer_sn = sn;        
    }
    const auto global_sn = buffer_sn + sn;

    auto double_str = [this](const double d)->String { return String{ d, 5 }.paddedLeft(' ', 8); };
    DBG(String{ ++console_line_count }.paddedLeft(' ', 3)
        << " | buffer: " << String{ buffer_cycle_count }.paddedLeft(' ', 4)
        << " | sn_to_last_beat: " << String{ global_sn - prev_beat_sn }.paddedLeft(' ', 7)
        << " | sn: " << String{ global_sn }.paddedLeft(' ', 7)
        << " | sn_time: " << sn_time.count()
        << " | prev_sn_time: " << prev_sn_time.count()
        << " | sn_ticks: " << (int64)link->clock().microsToTicks(sn_time)
        << " | prev_sn_ticks: " << (int64)link->clock().microsToTicks(prev_sn_time)
        << " | beat: " << double_str(beat)
        << " | phase: " << double_str(phase)
        << " | prev_sn_phase: " << double_str(prev_sn_phase)
        << " | " << midi_msg.getDescription());
 
    prev_beat_sn = global_sn;
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
                        + "\n" + (beat < 0. ? "count-in" : session.isPlaying() ? " playing" : " stopped")
                        + "\nBeats: " + String{ beat, 3 }.paddedLeft(' ', 8)
                        + "\nPhase: " + String{ phase, 3 }.paddedLeft(' ', 8) }, dontSendNotification);
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
    auto add_sound = [this, &sampler_note, &afm](const File& file, const int note)
    {
        if (file.existsAsFile())
        {
            auto note_range = BigInteger{};
            note_range.setBit(note);
            auto* reader = afm.createReaderFor(file);
            addSound(new SamplerSound(String{}, *reader, note_range, note, 0.01, 0.1,
                                      reader->lengthInSamples / reader->sampleRate));
            reader->~AudioFormatReader();
        }
    };
    add_sound(samples_folder.getChildFile("Kick.wav"), sampler_note);
    addVoice(new SamplerVoice{});
}
