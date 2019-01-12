#### Simple JUCE sampler sequencer synced using Ableton Link.
## How to use
- Open a second Link app such as Ableton Live on the same machine or another machine connected to the same LAN
- Click Link and StartStopSync to enable peers to control bpm and transport
- Adjust BPM in the second app while playing to witness glorious synchronization.
- Enable startstopsync in Ableton preferences to also have the ability to control transport from the second app.
## How to build your own Juce project using Ableton Link
#### The following instructions are already carried out for this project, they are simply provided for guidance on how to build new Juce projects that need Ableton Link.
### 1. Add Ableton Link dependency (via git command line)
- cd to your project directory, then optionally into your dependencies folder.
- Add link as a submodule in this location:    
`git submodule add https://github.com/Ableton/link link`
Then, because link also depends on another dependency, ASIO, it's best to recursively update (mandatory for older git versions):    
`git submodule update --init --recursive` .   
### 2. Include Header Search Paths
- Link requires 2 paths to be included in Juce:
1. "link/include"
2. "link/modules/asio-standalone/asio/include"
- These can be added in Projucer (for all builds) in "Project Settings -> Header Search Paths".
- Because this project places the link dependency in a "Dependencies" folder, the Header Search Paths field for this project looks like this:    
>../../Dependencies/link/include .
>../../Dependencies/link/modules/asio-standalone/asio/include

### 3. Configure Projucer
Link requires the platform to be specified, otherwise compiler/build errors result.
#### MacOSX & iOS
In the Projucer, go to the MacOSX and/or iOS IDE exporters (e.g. Xcode) and include the following under "Extra Preprocessor Definitions" .    
`LINK_PLATFORM_MACOSX=1`
#### Windows
- In the Projucer, go to the Windows related exporters (e.g. Visual Studio 2017) and include the following under "Extra Preprocessor Definitions" .   
`LINK_PLATFORM_WINDOWS=1` .   
- If you encounter further problems, you might need to also add this on a separate line:    
`_WIN32_WINNT=0x0501` .   
- For Windows, Link requires compiler flags to be provided. Below "Extra Prepocessor Definitions" you will find "Extra Compiler Flags" where you must add:
/D_SCL_SECURE_NO_WARNINGS /DLINK_BUILD_VLD=1 /MP /EHsc /wd4061 /wd4265 /wd4350 /wd4355 /wd4365 /wd4371 /wd4503 /wd4510 /wd4512 /wd4514 /wd4571 /wd4610 /wd4625 /wd4626 /wd4628 /wd4640 /wd4710 /wd4711 /wd4738 /wd4820 /wd4464 /wd4548 /wd4623 /wd4868 /wd5026 /wd5027 /wd4987 /wd4774 /wd5039 /wd4917
### 4. Include Header Files
- The necessary include:    
#include <ableton/Link.hpp>
- Although it's optional, I strongly recommend using, and thus including, the HostTimeFilter for the most accurate Link synchronization in Juce projects:    
#include <ableton/link/HostTimeFilter.hpp>
## Guidelines on implementing synchronization in your project
#### These guidelines are intended to be specific to Juce and an extension of the advice provided by Ableton on their github readme. Go there first for the basics, then come back here for some time savers.
### 1. Use the HostTimeFilter for SessionState synchronization.
Ableton recommends multiple ways of synchronization, based mainly on platform. However, Juce is multiplatform and has its own implementation of audio platform, engine, buffers, etc. In some Ableton examples, you'll find that the microseconds time that is used for synchronization at each audio buffer callback comes from calling link::clock().micros(). This appears to pull the microseconds time from whatever system clock link finds depending on OS platform. Although this is possible to do in Juce, my experience found that timing varied weirdly on different machines and I could not find the root of this issue (since they do not provide a Juce specific example).    
In other examples, you'll find the use of the HostTimeFilter. In short, the microseconds time given by this object is based not on some system clock, but on the sample time that you have counted manually at each audio buffer callback. The HostTimeFilter then uses some linear regression algorithm to determine the microseconds time that you need to synchronize with. Thus, it needs some "warming up".    
For this algorithm to work best, count your sample time as soon as link is instantiated, i.e. do not wait until the user presses play. For example, for a buffer size of 512, you want to increment the sample time by 512 in every audio buffer callback and do not reset this sample time to 0 when the user either stops or starts playback. Otherwise there will be some short period of time from the moment of playback where it sounds out of sync until link can DJ its way in.    
In the project, search for "sample_time" and "host_time_filter" to see how this is done.

### 2. Use numSamples provided by the Juce audio buffers for Link's latency correction
Link requires the output microseconds time be offset by some latency calculation based again on audio platform. In Ableton's examples, they sometimes rely on the audio platform to calculate and provide this latency (such as in ASIO), other times, they explicitly calculate this based on buffer size and sample rate. In my experience, the calculation used in the Jack example worked best, where using the device's buffer size worked best.    
At first, I used AudioDeviceManager::getCurrentDevice()->getOutputLatencyInSamples() for the calculation, but although mostly ok, found that the warm up time to synchronization was slightly longer than just using the buffer size.    
Instead, either use AudioDeviceManager::getCurrentDevice()->getCurrentBufferSizeSamples(), or just use the size of the buffer that's provided to you in the audio callbacks (such as AudioBuffer::getNumSamples() or AudioSourceChannelInfo::numSamples).    
See this project's calculate_output_time() method.

### 3. Prepare for time jumps and buffer overlaps.
Coming from vst development where samples and ppq beats get fed into your plugin at exactly the moment you expect them to come, your app may glitch out when adding Link and not preparing for these jumps and overlaps. At each buffer boundary the beat or phase values provided by link (via beatAtTime() and phaseAtTime) may show that they jump irregularly from buffer cycle to buffer cycle (e.g. buffer 1 might have beat values of 0.11 to 0.13 for samples 0 and 511, then buffer 2 has already jumped to values of 0.21 to 0.23), or overlap might occur (e.g. buffer 1 has 0.11 to 0.13 and buffer 2 has 0.12 to 0.14).    
In my experience, these things must be handled to prevent any unwanted artifacts. There are many approaches you can take, where my suggestions may be too convoluted or not accurate enough for your situation so I leave this to you. Seeing as Link uses real-time values for its algorithms, these can be hard to debug without setting up some test harness or mocking framework.

### 4. The std::chrono library provides for creating your own time types
Howard Hinnant (the almighty creator of time) explains this best in his talk at cppcon 2016. In this project, microseconds to samples is converted explicitly using the formula micros_to_samples = 1.0e6 / sample_rate. However, the chrono library provides conveniences to create types that do these conversions automatically.
https://www.youtube.com/watch?v=P32hvk8b13M

### 5. Use CachedValues for time sensitive state data.
In my other project, I found better efficiencies when using CachedValues for variables that the user requires low-latent interaction with that are also used in the audio thread. Bpm or tempo is the best example of this, where the user might use this paramater in real-time performance. Hopefully, you already appreciate the greatness of ValueTrees and are already using them to manager your app's state. What you might still be doing however, is something like state_tree.setProperty(bpm, .... etc. in your audio thread. This is probably fine for the most part, but I found that if there are too many of these calls, things can slow down or use too much cpu. Especially when Link is involved where not only your user controls the bpm, but any of his peers do as well.    
The best results come from wrapping this bpm/tempo value in a CachedValue. To initialize, simply call referTo() and feed it the corresponding tree and property. Then, instead of calling setProperty in either the audio thread or in Link's setTempoCallback(), you can simply just assign to this value as if it were a primitive.    
There is some catch though, David Rowland warns that because these reside on the message thread, benign data races may occur. He suggests that this can mostly be accounted for through atomic wrappers, although I personally have not found the need (yet) to go this far. You can find out more here (skip to around 32:00):    
https://www.youtube.com/watch?v=3IaMjH5lBEY
