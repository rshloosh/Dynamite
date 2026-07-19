/*
  ==============================================================================

    PluginProcessor.cpp

    JUCE AudioProcessor wrapper around the real-time-safe DynamiteEngine.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
NewProjectAudioProcessor::createParameterLayout()
{
    using AF = juce::AudioParameterFloat;
    using AC = juce::AudioParameterChoice;
    using AB = juce::AudioParameterBool;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AF> (juce::ParameterID { "threshold", 1 }, "Threshold",
                                      juce::NormalisableRange<float> (-40.0f, 20.0f), -10.0f));

    // Logarithmic-ish skew so the low end of the release range has more resolution.
    layout.add (std::make_unique<AF> (juce::ParameterID { "release", 1 }, "Release",
                                      juce::NormalisableRange<float> (0.05f, 5.0f, 0.0f, 0.3f), 0.25f));

    layout.add (std::make_unique<AF> (juce::ParameterID { "range", 1 }, "Range",
                                      juce::NormalisableRange<float> (0.0f, 60.0f), 40.0f));

    layout.add (std::make_unique<AF> (juce::ParameterID { "output", 1 }, "Output",
                                      juce::NormalisableRange<float> (-15.0f, 15.0f), 0.0f));

    layout.add (std::make_unique<AF> (juce::ParameterID { "mix", 1 }, "Mix",
                                      juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));

    // Choice order MUST match the DynamiteEngine enum values (0,1,2).
    layout.add (std::make_unique<AC> (juce::ParameterID { "source", 1 }, "Source",
                                      juce::StringArray { "INT", "DS-FM", "EXT" }, 0));   // 0=Internal,1=DsFm,2=External

    layout.add (std::make_unique<AC> (juce::ParameterID { "mode", 1 }, "Mode",
                                      juce::StringArray { "LIMIT", "EXPAND", "OUT" }, 0)); // 0=Limit,1=Expand,2=Bypass

    layout.add (std::make_unique<AC> (juce::ParameterID { "detector", 1 }, "Detector",
                                      juce::StringArray { "AVG", "PEAK", "GATE" }, 1));    // 0=Avg,1=Peak,2=Gate

    layout.add (std::make_unique<AB> (juce::ParameterID { "autoMakeup", 1 }, "Auto Makeup", false));
    layout.add (std::make_unique<AB> (juce::ParameterID { "scListen",   1 }, "SC Listen",   false));
    layout.add (std::make_unique<AB> (juce::ParameterID { "stereoLink", 1 }, "Stereo Link", true));
    layout.add (std::make_unique<AB> (juce::ParameterID { "safety",     1 }, "Safety",      true));

    return layout;
}

//==============================================================================
NewProjectAudioProcessor::NewProjectAudioProcessor()
   #ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                      #if ! JucePlugin_IsMidiEffect
                       #if ! JucePlugin_IsSynth
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)
                       #endif
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                      #endif
                        ),
       parameters (*this, nullptr, juce::Identifier ("Dynamite"), createParameterLayout())
   #endif
{
    // Cache the raw atomic pointers once so the audio thread never does a
    // string lookup. These are stable for the lifetime of the APVTS.
    pThreshold  = parameters.getRawParameterValue ("threshold");
    pRelease    = parameters.getRawParameterValue ("release");
    pRange      = parameters.getRawParameterValue ("range");
    pOutput     = parameters.getRawParameterValue ("output");
    pMix        = parameters.getRawParameterValue ("mix");
    pSource     = parameters.getRawParameterValue ("source");
    pMode       = parameters.getRawParameterValue ("mode");
    pDetector   = parameters.getRawParameterValue ("detector");
    pAutoMakeup = parameters.getRawParameterValue ("autoMakeup");
    pScListen   = parameters.getRawParameterValue ("scListen");
    pStereoLink = parameters.getRawParameterValue ("stereoLink");
    pSafety     = parameters.getRawParameterValue ("safety");
}

//==============================================================================
const juce::String NewProjectAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool NewProjectAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool NewProjectAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool NewProjectAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double NewProjectAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int NewProjectAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope well with 0 programs, so report at least 1.
}

int NewProjectAudioProcessor::getCurrentProgram()
{
    return 0;
}

void NewProjectAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String NewProjectAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void NewProjectAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void NewProjectAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numChannels = juce::jmax (getMainBusNumInputChannels(),
                                        getMainBusNumOutputChannels());

    engine.prepare (sampleRate, samplesPerBlock, numChannels);
    setLatencySamples (engine.getLatencySamples());
}

void NewProjectAudioProcessor::releaseResources()
{
    engine.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NewProjectAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn  = layouts.getMainInputChannelSet();

    // Main bus must be mono or stereo.
    if (mainOut != juce::AudioChannelSet::mono()
     && mainOut != juce::AudioChannelSet::stereo())
        return false;

    // Main input must match the main output.
    if (mainIn != mainOut)
        return false;

    // Sidechain (second input bus) may be disabled, or mono/stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled()
         && sc != juce::AudioChannelSet::mono()
         && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
  #endif
}
#endif

//==============================================================================
void NewProjectAudioProcessor::pushParametersToEngine() noexcept
{
    engine.setThresholdDbv   (pThreshold->load());
    engine.setReleaseSeconds (pRelease->load());
    engine.setRangeDb        (pRange->load());
    engine.setOutputDb       (pOutput->load());
    engine.setMix            (pMix->load());

    engine.setSource   (static_cast<DynamiteEngine::Source>   (juce::jlimit (0, 2, (int) pSource->load())));
    engine.setMode     (static_cast<DynamiteEngine::Mode>     (juce::jlimit (0, 2, (int) pMode->load())));
    engine.setDetector (static_cast<DynamiteEngine::Detector> (juce::jlimit (0, 2, (int) pDetector->load())));

    engine.setAutoMakeup      (pAutoMakeup->load() > 0.5f);
    engine.setSidechainListen (pScListen->load()   > 0.5f);
    engine.setStereoLink      (pStereoLink->load() > 0.5f);
    engine.setSafetyCeiling   (pSafety->load()     > 0.5f);
}

template <typename FloatT>
void NewProjectAudioProcessor::processImpl (juce::AudioBuffer<FloatT>& buffer) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    pushParametersToEngine();

    // Main audio bus, processed in place (input aliases output bus 0).
    auto mainBuffer = getBusBuffer (buffer, false, 0);

    // Optional sidechain: second input bus. Pass &sidechain only when present
    // and enabled; otherwise nullptr so the engine falls back to the main input.
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
    {
        auto sidechain = getBusBuffer (buffer, true, 1);
        if (sidechain.getNumChannels() > 0)
        {
            engine.process (mainBuffer, &sidechain);
            return;
        }
    }

    engine.process (mainBuffer, nullptr);
}

void NewProjectAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processImpl (buffer);
}

void NewProjectAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processImpl (buffer);
}

//==============================================================================
bool NewProjectAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* NewProjectAudioProcessor::createEditor()
{
    return new NewProjectAudioProcessorEditor (*this);
}

//==============================================================================
void NewProjectAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void NewProjectAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}
