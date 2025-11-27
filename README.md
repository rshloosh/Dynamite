# Dynamite Audio Processor

A vintage-style dynamic processor with compressor, gate, de-esser, and limiter.

## Recent Fixes - Threshold Behavior

The plugin has been fixed to properly handle the threshold control, particularly at 0 dB and above. The following changes were implemented:

### 1. Fixed Threshold Parameter Connection

- Fixed the calculateCompressorGain function to directly use the threshold value when determining compression amount
- Enhanced envelope tracking to respond properly to threshold changes
- Ensured that changing the threshold parameter immediately affects the compression amount
- Improved debug logging to track threshold effect on compression

### 2. Fixed Absolute Bypass Logic

When the threshold is set to 0 dB or higher, the plugin now properly bypasses all processing stages:

- The `process()` method now zeros out all processing states
- All gain values (gate, compressor, de-esser, limiter) are set to unity (1.0)
- Only the output gain is applied to the signal

### 3. Added Early Exit in Processing Functions

Added immediate early returns in the following processing functions when threshold is 0 dB or above:

- `updateGateEnvelope` - Sets gateEnvelope to -120.0 dB and gateGain to 1.0
- `updateDeesserEnvelope` - Sets deesserEnvelope to 0.0
- `updateLimiterEnvelope` - Sets limiterEnvelope to 0.0
- `calculateGateGain` - Returns 1.0 immediately (no gating)
- `calculateDeesserGain` - Returns 1.0 immediately (no de-essing)

### 4. Improved Threshold Parameter Behavior

- Added envelope tracking based on threshold position
- When threshold moves from above 0 dB to below 0 dB, the envelope is initialized at a reasonable level
- When threshold is lowered, the envelope position is maintained relative to the threshold
- Adjusted attack/release times for more responsive threshold tracking

### 5. Additional Logging

- Added enhanced debug logging to verify bypass functionality
- Logs now confirm states of all gain stages during bypass
- Added specific logging for threshold-related behaviors

## Expected Behavior

With these fixes, the Dynamite plugin should now:

1. **Threshold Directly Affects Compression**:
   - Higher threshold values (closer to 0 dB) = less compression
   - Lower threshold values (more negative) = more compression
   - Changes to threshold are immediately reflected in the amount of compression

2. **At Threshold = 0 dB**: 
   - Apply zero processing (no compression, gating, de-essing, limiting)
   - Apply only the output gain
   - Show zero gain reduction on meters

3. **At Threshold < 0 dB**:
   - Apply normal dynamic processing based on settings
   - Show appropriate gain reduction on meters
   - Threshold directly controls how much of the signal is compressed

## Building

To build the plugin:

```bash
cd /path/to/Dynamite/NewProject/Builds/MacOSX
xcodebuild -project Dynamite.xcodeproj -configuration Release -scheme "Dynamite - VST3"
```

## Installation

Copy the built plugin to your VST3 plugins directory:

```bash
cp -r /path/to/Dynamite/NewProject/Builds/MacOSX/build/Release/Dynamite.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

## Troubleshooting

If the threshold knob still does not behave correctly, please check:

1. Ensure you're using the latest build of the plugin
2. Check your DAW's plugin cache - you may need to reset it
3. Try removing and reinstalling the plugin
4. Ensure no other settings (range, ratio) are interfering with the threshold behavior 