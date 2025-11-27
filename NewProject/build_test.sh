#!/bin/bash

echo "Building threshold test program..."

# Get JUCE modules path
JUCE_MODULES_PATH="/Applications/JUCE/modules"
JUCE_INCLUDES="-I$JUCE_MODULES_PATH -I$JUCE_MODULES_PATH/juce_audio_basics/buffers -I$JUCE_MODULES_PATH/juce_core/maths -I./JuceLibraryCode -I./Source"

# Compile the test program
clang++ -std=c++17 test_threshold.cpp Source/DynamiteProcessor.cpp \
  -o threshold_test $JUCE_INCLUDES \
  -I/Applications/JUCE/modules \
  -I./JuceLibraryCode \
  -framework Cocoa \
  -framework CoreAudio \
  -framework CoreMIDI \
  -framework AudioToolbox \
  -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
  -DJUCE_STANDALONE_APPLICATION=1

# Check if compilation was successful
if [ $? -eq 0 ]; then
  echo "Build successful, running test..."
  ./threshold_test
  
  # Display test results
  if [ -f "threshold_test_results.txt" ]; then
    echo "Test results:"
    cat threshold_test_results.txt
  else
    echo "Error: Test results file not found."
  fi
else
  echo "Build failed."
fi 