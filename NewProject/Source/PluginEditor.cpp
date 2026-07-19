/*
  ==============================================================================

    PluginEditor.cpp

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
NewProjectAudioProcessorEditor::NewProjectAudioProcessorEditor (NewProjectAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), dynamiteComponent (p)
{
    addAndMakeVisible (dynamiteComponent);

    // The faceplate chooses its own deliberate size; the editor matches it.
    setSize (dynamiteComponent.getWidth(), dynamiteComponent.getHeight());
    setResizable (false, false);
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor() = default;

//==============================================================================
void NewProjectAudioProcessorEditor::paint (juce::Graphics& g)
{
    // The DynamiteComponent covers the whole editor; this is only ever seen
    // for a frame before layout settles.
    g.fillAll (juce::Colours::black);
}

void NewProjectAudioProcessorEditor::resized()
{
    dynamiteComponent.setBounds (getLocalBounds());
}
