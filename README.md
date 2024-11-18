# ESP32-S3-MIDI-Example

Based off the example from Espressif, this program reads some analog inputs, filters the values, and sends them as a MIDI control change messages.

When setting up your idf project, set CONFIG_TINYUSB_MIDI_COUNT=1 in the sdkconfig.
