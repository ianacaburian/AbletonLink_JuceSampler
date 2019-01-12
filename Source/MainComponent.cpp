#include "MainComponent.h"
// ========================================================================================
MainComponent::MainComponent()
    : shared_engine_data({ 0., false, false, 1., false })
    , lock_free_engine_data{ shared_engine_data }
    , abe_synth{ middle_c }

    // GUI Components
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
                                                      &lb_quantum, &lb_bpm, &lb_velocity, &main_display }) {
        addAndMakeVisible(c);
    }
    setSize(600, 400);
    startTimerHz(30);

    // GUI component callbacks
    tb_settings.onClick = [this] { show_audio_device_settings(); };
    tb_link.onClick = [this] { link->enable(tb_link.getToggleState()); };
    tb_play.onClick = [this] { std::lock_guard<std::mutex> lock{ engine_data_guard };
                               shared_engine_data.request_start = true; };
    tb_stop.onClick = [this] { std::lock_guard<std::mutex> lock{ engine_data_guard };
                               shared_engine_data.request_stop = true; };
    tb_sync.onClick = [this] { link->enableStartStopSync(tb_sync.getToggleState()); };
    sl_quantum.onValueChange = [this] { std::lock_guard<std::mutex> lock{ engine_data_guard };
                                        shared_engine_data.quantum = sl_quantum.getValue(); };
    sl_bpm.onValueChange = [this] { std::lock_guard<std::mutex> lock{ engine_data_guard };
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
    midi_buffer.clear();
    bufferToFill.clearActiveBufferRegion();
    
    const auto sample_rate = current_device->getCurrentSampleRate();
    calculate_output_time(sample_rate, bufferToFill.numSamples);

    // Extract info from link and modify its state as per user requests.
    const auto engine_data = pull_engine_data();
    process_session_state(engine_data);
    
    // Play the beat
    if (is_playing) {
        trigger_sampler(sample_rate, engine_data.quantum, bufferToFill.numSamples);
    }
    abe_synth.renderNextBlock(*bufferToFill.buffer, midi_buffer, 0, bufferToFill.numSamples);
    sample_time += bufferToFill.numSamples;
}
// ========================================================================================
void MainComponent::releaseResources()
{}
// ========================================================================================
// Link Synchronization Methods
// ========================================================================================
void MainComponent::calculate_output_time(const double sample_rate, const int buffer_size)
{
    // Synchronize host time to reference the point when its output reaches the speaker.
    const auto host_time =  host_time_filter.sampleTimeToHostTime(sample_time);
    const auto output_latency = std::chrono::microseconds{ std::llround(1.0e6 * buffer_size / sample_rate) };
    output_time = output_latency + host_time;
}
// ========================================================================================
MainComponent::EngineData MainComponent::pull_engine_data()
{   // Safely operate on data isolated from user changes.
    auto engine_data = EngineData{};
    if (engine_data_guard.try_lock()) {
        engine_data.requested_bpm = shared_engine_data.requested_bpm;
        shared_engine_data.requested_bpm = 0;
        
        engine_data.request_start = shared_engine_data.request_start;
        shared_engine_data.request_start = false;
        
        engine_data.request_stop = shared_engine_data.request_stop;
        shared_engine_data.request_stop = false;
        
        lock_free_engine_data.quantum = shared_engine_data.quantum;
        lock_free_engine_data.startstop_sync = shared_engine_data.startstop_sync;
        
        engine_data_guard.unlock();
    }
    else
        DBG("entry failed");
    engine_data.quantum = lock_free_engine_data.quantum;
    return engine_data;
}
// ========================================================================================
void MainComponent::process_session_state(const EngineData& engine_data)
{
    session = std::make_unique<ableton::Link::SessionState>(link->captureAudioSessionState());

    if (engine_data.request_start)
        session->setIsPlaying(true, output_time);

    if (engine_data.request_stop)
        session->setIsPlaying(false, output_time);

    if (!is_playing && session->isPlaying()) {   // Reset the timeline so that beat 0 corresponds to the time when transport starts
        session->requestBeatAtTime(0., output_time, engine_data.quantum);
        is_playing = true;
    }
    else if (is_playing && !session->isPlaying())
        is_playing = false;

    if (engine_data.requested_bpm > 0) // Set the newly requested tempo from the beginning of this buffer.
        session->setTempo(engine_data.requested_bpm, output_time);

    link->commitAudioSessionState(*session); // Timeline modifications are complete, commit the results
}
// ========================================================================================
void MainComponent::trigger_sampler(const double sample_rate, const double quantum, const int buffer_size)
{   // Taken from Ableton's linkhut example found on their github.
    const auto micros_per_sample = 1.0e6 / sample_rate;
    for (std::size_t i = 0; i < buffer_size; ++i) {
        // Compute the host time for this sample and the last.
        const auto host_time = output_time + std::chrono::microseconds(llround(i * micros_per_sample));
        const auto prev_host_time = host_time - std::chrono::microseconds(llround(micros_per_sample));
        
        // Only make sound for positive beat magnitudes. Negative beat
        // magnitudes are count-in beats.
        if (session->beatAtTime(host_time, quantum) >= 0.) {
            
            // If the phase wraps around between the last sample and the
            // current one with respect to a 1 beat quantum, then a click
            // should occur.
            if (session->phaseAtTime(host_time, beat_length)
                < session->phaseAtTime(prev_host_time, beat_length)) {
                
                const auto float_velocity = static_cast<float>(sl_velocity.getValue());
                const auto midi_msg = MidiMessage::noteOn(1, middle_c, float_velocity);
                midi_buffer.addEvent(midi_msg, static_cast<int>(i));
            }
        }
    }
}
// ========================================================================================
// Utility Audio Methods
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
    while (!samples_folder.isRoot()) {
        samples_folder = samples_folder.getParentDirectory();
        if (samples_folder.getFileName() == "AbletonLink_JuceSampler") {
            samples_folder = samples_folder.getChildFile("Samples");
            break;
        }
    }
    if (samples_folder.getFileName() != "Samples") {
        AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon,
                                          TRANS("Samples folder not found..."),
                                          TRANS("There was an error while trying to find the samples folder"));
        return;
    }
    AudioFormatManager afm;
    afm.registerFormat(new WavAudioFormat{}, true);
    auto add_sound = [&](const String& file_name, const int note) {
        const auto file = File{ samples_folder.getChildFile(file_name) };
        if (file.existsAsFile()) {
            auto note_range = BigInteger{};
            note_range.setBit(note);
            std::unique_ptr<AudioFormatReader> reader{ afm.createReaderFor(file) };
            addSound(new SamplerSound(String{}, *reader, note_range, note, 0.01, 0.1,
                                      reader->lengthInSamples / reader->sampleRate));            
        }
        else {
            AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon,
                                              TRANS("Failed to open file..."),
                                              TRANS("There was an error while trying to load the file: FLNM")
                                              .replace ("FLNM", "\n" + samples_folder.getFullPathName()) + "\n\n");
        }
    };
    add_sound(sample_file_name, sampler_note);
    addVoice(new SamplerVoice{});
    addVoice(new SamplerVoice{});
}
// ========================================================================================
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
    main_display.setFont(Font{ Font::getDefaultMonospacedFontName(), "mono", main_display.getHeight() / 4.f });

    const auto sl_height = bounds.getHeight();
    auto&& sliders = std::initializer_list<Component*>{ &sl_quantum, &sl_bpm, &sl_velocity };
    for (auto* s : sliders) {
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
    const auto app_session = link->captureAppSessionState();
    const auto beat = app_session.beatAtTime(time, shared_engine_data.quantum);
    const auto phase = app_session.phaseAtTime(time, shared_engine_data.quantum);
    main_display.setText(
        String{ (beat < 0. ? "count-in" : app_session.isPlaying() ? " playing" : " stopped") }
        + "\nBeats: " +  double_str(beat)
        + "\nPhase: " + double_str(phase)
        + "\nPeers: " + String{ link->numPeers() }
        , dontSendNotification);
}
String MainComponent::double_str(const double d) { return String::formatted("%8.3f", d); }
