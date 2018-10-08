#include "MainComponent.h"
// ========================================================================================
MainComponent::MainComponent()
    : shared_engine_data({ 0., false, false, 4., false })
    , lock_free_engine_data{ shared_engine_data }
    , abe_synth{ sampler_note }
    , buffer_sn{ 0 }
    , prev_beat_sn{ 0 }
    , is_playing{ false }
    , console_line_count{ 0 }
    , tb_settings{ "Audio Settings" }
    , tb_link{ "Link" }
    , tb_play{ "Play" }
    , tb_stop{ "Stop" }
    , tb_sync{ "Start Stop Sync" }
    , lb_quantum{ "", "Quantum" }
    , lb_bpm{ "", "BPM" }
    , lb_velocity{ "", "Velocity" }
{    
    // GUI initialization
    for (auto* c : std::initializer_list<Component*>{ &tb_settings, &tb_link, &tb_play, &tb_stop, &tb_sync, 
                                                      &sl_quantum, &sl_bpm, &sl_velocity,
                                                      &lb_quantum, &lb_bpm, &lb_velocity, &main_display })
        addAndMakeVisible(c);    
    setSize(600, 400);
    startTimerHz(30);

    // GUI component callbacks
    tb_settings.onClick = [this] { show_audio_device_settings(); };
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
    // GUI component initialization
    tb_link.setClickingTogglesState(true);
    tb_sync.setClickingTogglesState(true);
    sl_quantum.setRange(1., 8., 1.);
    sl_quantum.getValueObject() = shared_engine_data.quantum;
    lb_quantum.attachToComponent(&sl_quantum, true);
    sl_bpm.setRange(10., 200.);
    sl_bpm.getValueObject() = 120.;
    lb_bpm.attachToComponent(&sl_bpm, true);
    sl_velocity.setRange(0., 1.);
    sl_velocity.getValueObject() = 0.2f;
    lb_velocity.attachToComponent(&sl_velocity, true);    
    for (auto* s : { &sl_quantum, &sl_bpm, &sl_velocity }) { s->setSliderStyle(Slider::SliderStyle::LinearBar);
                                                             s->setTextBoxIsEditable(false); }
    // GUI component appearance
    auto&& laf = getLookAndFeel();
    laf.setColour(TextButton::buttonColourId, Colours::black);
    laf.setColour(TextButton::buttonOnColourId, Colours::black);
    laf.setColour(TextButton::textColourOnId, Colours::limegreen);
    laf.setColour(Label::backgroundColourId, Colours::black);
    laf.setColour(Label::textColourId, Colours::white);    
    main_display.setJustificationType(Justification::right);

    // Audio initialization
    setAudioChannels (0, 2);
    link.reset(new ableton::Link{ sl_bpm.getValue() });
    link->setTempoCallback([this](const double p) { sl_bpm.getValueObject() = p; });
    link->enable(false);
}
// ========================================================================================
MainComponent::~MainComponent()
{
    if (link->isEnabled())
        link->enable(false);
    shutdownAudio();
}
// ========================================================================================
void MainComponent::prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRate)
{
    abe_synth.setCurrentPlaybackSampleRate(sampleRate);    
}
// ========================================================================================
void MainComponent::getNextAudioBlock (const AudioSourceChannelInfo& bufferToFill)
{    
    auto* current_device = deviceManager.getCurrentAudioDevice();
    if (!current_device || !link)
        return;

    const auto sample_rate = current_device->getCurrentSampleRate();
    const auto micros_per_sample = 1e6 / sample_rate; // number of microseconds that elapse between samples 

    // Synchronize host time to reference the point when its output reaches the speaker.
    auto output_host_time = link->clock().micros();
    output_host_time += Micros{ llround(micros_per_sample * current_device->getOutputLatencyInSamples()) };
    
    // Clear buffers
    mb.clear();
    bufferToFill.clearActiveBufferRegion();

    // Extract info from link and modify its state.
    const auto engine_data = pull_engine_data();
    auto session = process_session_state(engine_data, output_host_time);

    // Sequence the synth. 
    if (is_playing)
    {
        if (USE_BEATS_FOR_SEQUENCING) // Beats: sequencer-focused approach, much more simpler than the phase approach.
            play_sequencer_beats(micros_per_sample, bufferToFill.numSamples, output_host_time, session);

        else // Phase: audio-based approach (mirrors Ableton's "linkhut" example more closely)
            play_sequencer_phase(micros_per_sample, bufferToFill.numSamples, output_host_time, engine_data.quantum, session);
    }
    abe_synth.renderNextBlock(*bufferToFill.buffer, mb, 0, bufferToFill.numSamples);

    // Update "debug-only" vars
    buffer_sn += bufferToFill.numSamples;
    ++buffer_cycle_count;
}
// ========================================================================================
void MainComponent::releaseResources()
{}
// ========================================================================================
ableton::Link::SessionState MainComponent::process_session_state(const EngineData& engine_data, 
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
// ========================================================================================
void MainComponent::play_sequencer_beats(const double micros_per_sample, const int num_samples, 
                                         const Micros host_time, const ableton::Link::SessionState& session)
{   // Simply looks at the beat at the start of the buffer, and the beat at the end of the buffer.        
    // The synth is triggered when a new beat occurs within this buffer cycle.
    const auto beat = session.beatAtTime(host_time, 1);
    const auto next_beat = ceil(beat);
    const auto next_beat_time = session.timeAtBeat(next_beat, 1);
    const auto end_time = host_time + Micros{ llround(micros_per_sample * num_samples) };

    if (next_beat_time < end_time) // Downbeat occurs within this buffer cycle.
    {   
        // microseconds from the buffer start to the next beat.
        const auto micros_to_next_beat = (next_beat_time - host_time).count();

        // sample number of the next beat.
        const auto next_beat_sn = static_cast<int>(llround(micros_to_next_beat / micros_per_sample));

        // buffer the next midi message
        const auto midi_msg = MidiMessage::noteOn(1, sampler_note, static_cast<float>(sl_velocity.getValue()));
        mb.addEvent(midi_msg, next_beat_sn);
        
        // print state info to console
        debug_state(true, next_beat_sn, next_beat_time, next_beat, midi_msg);
    }
}
// ========================================================================================
void MainComponent::play_sequencer_phase(const double micros_per_sample, const int num_samples, 
                                         const Micros host_time, const double quantum, 
                                         const ableton::Link::SessionState& session)
{   // This is taken from renderMetronome in Ableton's "linkhut" example.
    // Each sample is checked to see if the phase has wrapped, meaning that a downbeat occurs.
    for (auto sn = 0; sn != num_samples; ++sn)
    {
        const auto sn_time = host_time + Micros{ llround(sn * micros_per_sample) };
        const auto prev_sn_time = sn_time - Micros{ llround(micros_per_sample) };

        const auto beat = session.beatAtTime(sn_time, quantum);
        const auto phase = session.phaseAtTime(sn_time, 1);
        const auto prev_sn_phase = session.phaseAtTime(prev_sn_time, 1);

        const auto is_count_in = beat < 0.; // This beat occurs before the bar begins (anacrusis).
        const auto is_downbeat = phase < prev_sn_phase; // The phase has wrapped since last sample.

        if (!is_count_in && is_downbeat) // trigger sample
        {
            const auto midi_msg = MidiMessage::noteOn(1, sampler_note, static_cast<float>(sl_velocity.getValue()));
            mb.addEvent(midi_msg, sn);
            debug_state(false, sn, sn_time, beat, midi_msg, prev_sn_time, phase, prev_sn_phase);
        }
    }
}
// ========================================================================================
MainComponent::EngineData MainComponent::pull_engine_data()
{   // Safely operate on data isolated from user changes.
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
// ========================================================================================
void MainComponent::debug_state(const bool beats, const int sn, const Micros& sn_time, 
                                const double beat, const MidiMessage& midi_msg, const Micros& prev_sn_time,
                                const double phase, const double prev_sn_phase)
{    
    if (beat < 1.)
    {
        prev_beat_sn = buffer_cycle_count = 0;
        buffer_sn = sn;        
    }
    const auto global_sn = buffer_sn + sn;

    auto double_str = [this](const double d)->String { return String{ d, 5 }.paddedLeft(' ', 8); };
    DBG(String{ ++console_line_count }.paddedLeft(' ', 3)

        // The number of elapsed buffer cycles is counted, starting at 0 when play is started.
        << " | buffer: " << String{ buffer_cycle_count }.paddedLeft(' ', 4)

        // The number of samples since last beat, this should be fairly consistent when tempo is steady.
        << " | sn_to_last_beat: " << String{ global_sn - prev_beat_sn }.paddedLeft(' ', 7)

        // The number of elapsed samples is counted beyond each buffer cycle, starting at 0 when play is started.
        << " | sn: " << String{ global_sn }.paddedLeft(' ', 7)

        // The host time that has been computed for the above sample number. 
        << " | sn_time: " << sn_time.count()

        // This is computed using (sn_time - micros_per_sample), 
        // i.e. the previous sample number may actually have had a slightly different
        // time computed when the computation took place.
        << (beats ? "" : " | prev_sn_time: " + prev_sn_time.count())

        << " | beat: " << double_str(beat)
        << (beats ? "" : " | phase: " + double_str(phase))
        << (beats ? "" : " | prev_sn_phase: " + double_str(prev_sn_phase)) // this is derived from prev_sn_time.
        << " | " << midi_msg.getDescription());
 
    prev_beat_sn = global_sn;
}
// ========================================================================================
void MainComponent::show_audio_device_settings()
{
    AudioDeviceSelectorComponent selector{ deviceManager, 0, 2, 0, 2, false, false, true, false };
    selector.setSize(getWidth() / 2, getHeight());
    DialogWindow::LaunchOptions dialog;
    dialog.content.setNonOwned(&selector);
    dialog.dialogTitle = "Audio Settings";
    dialog.componentToCentreAround = this;
    dialog.dialogBackgroundColour = getLookAndFeel().findColour(ResizableWindow::backgroundColourId);    
    dialog.useNativeTitleBar = false;
    dialog.resizable = false;
    dialog.useBottomRightCornerResizer = false;
    dialog.runModal();
}
// ========================================================================================
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
// GUI Methods ============================================================================
// ========================================================================================
void MainComponent::paint(Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}
// ========================================================================================
void MainComponent::resized()
{
    auto&& bounds = getLocalBounds().toFloat();
    const auto width = bounds.getWidth();

    auto&& button_bounds = bounds.removeFromTop(bounds.getHeight() / 4);
    auto&& buttons = std::initializer_list<Component*>{ &tb_settings, &tb_link, &tb_play, &tb_stop, &tb_sync };
    for (auto* b : buttons)
        b->setBounds(button_bounds.removeFromLeft(width / buttons.size()).toNearestIntEdges());

    main_display.setBounds(bounds.removeFromTop(bounds.getHeight() / 2).toNearestIntEdges().withRight(tb_stop.getRight()));
    main_display.setFont(Font{ "consolas", "mono", main_display.getHeight() / 4.f });

    const auto sl_height = bounds.getHeight();
    auto&& sliders = std::initializer_list<Component*>{ &sl_quantum, &sl_bpm, &sl_velocity };
    for (auto* s : sliders)
    {
        auto&& slider_bounds = bounds.removeFromTop(sl_height / sliders.size());
        s->setBounds(slider_bounds.removeFromRight(0.8f * width).toNearestIntEdges());
    }
}
// ========================================================================================
void MainComponent::timerCallback()
{
    update_label();
}
// ========================================================================================
void MainComponent::update_label()
{
    const auto time = link->clock().micros();
    const auto session = link->captureAppSessionState();
    const auto beat = session.beatAtTime(time, shared_engine_data.quantum);
    const auto phase = session.phaseAtTime(time, shared_engine_data.quantum);
    main_display.setText(String{ "Peers: " + String{ link->numPeers() }
                        + "\n" + (beat < 0. ? "count-in" : session.isPlaying() ? " playing" : " stopped")
                        + "\nBeats: " + String{ beat, 3 }.paddedLeft(' ', 8)
                        + "\nPhase: " + String{ phase, 3 }.paddedLeft(' ', 8) }, dontSendNotification);
}
