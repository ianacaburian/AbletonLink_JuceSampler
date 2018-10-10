# AbeLinkolnsJuce
Simple JUCE sampler sequencer synced using Ableton Link

# Run
- When link disabled or if there are no peers, play/stop occurs immediately upon clicking.
- When link enabled with at least 1 peer, play should be queued to occur when quantum wraps to 0.
- If Start Stop Sync is enabled, transport of peers is controlled if their sync is also enabled.

# Dependencies
- JUCE: use global path for modules
- link: included using updated asio for windows
