/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
NewProjectAudioProcessorEditor::NewProjectAudioProcessorEditor (NewProjectAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), dynamiteComponent(p)
{
    // Add and make visible the custom Dynamite component
    addAndMakeVisible(dynamiteComponent);
    
    // Set the editor size based on our component
    setSize (dynamiteComponent.getWidth(), dynamiteComponent.getHeight());
    
    // Set resizable to false to maintain the exact UI
    setResizable(false, false);
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor()
{
}

//==============================================================================
void NewProjectAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Let the Dynamite component handle all drawing
    g.fillAll(juce::Colours::black);
}

void NewProjectAudioProcessorEditor::resized()
{
    // Set the Dynamite component to fill the entire editor
    dynamiteComponent.setBounds(getLocalBounds());
}
