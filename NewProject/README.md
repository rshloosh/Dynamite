# Dynamite Compressor

A JUCE-based digital recreation of the "Dyna-mite" vintage compressor.

## Features

- Full replica of the Dyna-mite compressor hardware
- 64-bit double precision audio processing
- Support for sample rates from 44.1kHz to 192kHz
- Mono and stereo operation
- VST3 and AU plugin formats

## Controls

- **Threshold**: Sets the level at which compression begins (-40 to 0 dB)
- **Release**: Controls how quickly the compressor recovers after compression (0.05 to 5.0 seconds)
- **Gain Reduction**: 8 settings for compression ratio (1, 3, 6, 10, 15, 20, 30, and 40 dB)
- **Range**: Sets the maximum amount of gain reduction (0 to 50 dB)
- **Output**: Output gain compensation (-15 to +15 dB)

### Mode Switches

- **Int/Ext**: Internal or external detection
- **FM/DS**: Frequency modulation or direct signal mode
- **Limit Mode**: Toggle between compression and limiting
- **Avg/Peak**: Selects average or peak detection for the sidechain

## Building

1. Open NewProject.jucer with the JUCE Projucer
2. Generate the project files for your IDE (Xcode, Visual Studio, etc.)
3. Build the project from your IDE

## Requirements

- JUCE 6.0 or higher
- C++14 compatible compiler
- VST3 SDK (included with JUCE)
- Audio Units SDK (Mac only)

## License

This software is provided for educational purposes only. All trademarks and copyrights belong to their respective owners.

## Credits

- Original hardware design by Valley People Inc.
- Digital recreation implemented for educational purposes 