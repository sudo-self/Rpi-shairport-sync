# Get The Best from Shairport Sync
Shairport Sync was designed to run best on dedicated stand-alone low-power "headless" Linux/FreeBSD systems with ALSA as the audio system
and with a decent CD-quality Digital to Analog Converter (DAC).

## CPU Power and Memory
Computer power and memory requirements are modest – a Raspberry Pi 2 or better, including the Pi Zero 2 W, is fine.
Unfortunately, while the original Raspberry Pi and the Pi Zero are powerful enough for AirPlay operation,
they are not really suitable for AirPlay 2 operation.

## CPU Clock
For best performance, Shairport Sync requires a stable and accurate system clock.
This is because the output DAC's output rate is normally determined by the system clock (exceptionally, some high-end USB streamers use their own built-in clocks).
If the clock drifts, or if its actual frequency is far from its nominal frequency, Shairport Sync will have to do more interpolation,
which inevitably must degrade the audio fidelity, even if it is very hard to hear.
Some very old laptops are known to have inaccurate clocks and and some embedded systems can suffer from temperature-related clock drift.

Recent Raspberry Pis seem to have very accurate clocks with low drift.

## Linux
The best kind of Linux for Shairport Sync is a "bare" Linux.
Raspberry Pi OS Lite, Debian Minimal Server, Ubuntu Server, Fedora Server and Arch Linux (Minimal Configuration) are good examples.

Shairport Sync also runs on "desktop" Linuxes such as Raspberry Pi OS with desktop, Debian with a desktop environment,
Ubuntu Desktop and Fedora Workstation.
Desktop Linuxes are less suitable because they almost always use a sound server like PulseAudio or PipeWire.
These can interfere with Shairport Sync, which needs direct and exclusive access to the audio hardware.

## DAC
A good Digital to Analog Converter (DAC) will have a huge influence on the quality of the audio.

Shairport Sync runs at 44,100 frames per second (FPS), each frame consisting of a pair of signed 16 bit linear samples, one for left and one for right.
Shairport Sync will take advantage to 24- or 32-bit DACs if available, and will run at 44,100 or 88,200, 176,400 or 352,800 FPS if necessary,
though there is no advantage to the higher rates. The 44,100 FPS rate was chosen to match the rate of AirPlay.

Good DACs are available at a very wide range of prices, from low-cost USB "Sound Cards" to very high-end HiFi streaming DACs.

In the Raspberry Pi world, many very good low-cost I2S DACs, some with integrated amplifiers, are available. The DAC powering the Pi's built-in audio jack is not great, however. While it may be good enough for trying out Shairport Sync or for casual listening, it has a very limited frequency response and can generate very large transients when it starts up. A separate DAC will transform the output quality.

**Note** 
Make sure that the DAC is capable of 44,100 FPS operation – this is really mandatory.
Most recent DACs are okay, but some older DACs will only run at 48,000 FPS or multiples of it, and Shairport Sync can not use them.
## Maximum Output Level
The `volume_max_db` setting allows you to reduce the maximum level of DAC output to prevent possible overloading of the amplifier or premplifier it feeds.
## Volume Range
The volume range is the difference (technically the ratio, expressed in dB) between the highest and lowest level of the volume control. Ideally, this should give the highest volume at the high end and a barely audible sound at the lowest level. Typical volume ranges are 60 dB to 75dB. If the range is much less than this, the difference between high and low volume won't seem large enough to the listener. If the range is much more, much of the low end of the volume control range will be inaudible. (The built-in DAC of the Raspberry Pi has this problem.) Use the `volume_range_db` setting to set the volume range. If the range you request is greater than the range available in the hardware mixer, the built-in attenuator will be used to make up the difference.
## Volume Control
Audio is sent at full volume in AirPlay and AirPlay 2 with separate information being sent to set the actual volume. This volume information can be used in four ways:
* It can be used to control a built-in attenuator. This is the default.
* It can be used to control a mixer built in to the DAC. To use a mixer, set the `mixer_control_name` in the configuration file to the name of the mixer you wish to use.
* It can be ignored by Shairport Sync. This may be appropriate when you want the volume to be controlled by just one control, typically an audio system's  volume control knob or the volume control of a car radio. To make Shairport Sync ignore the volume information, set `ignore_volume_control` to `"yes"`.
* AirPlay volume changes cause events when an executable – a program or executable script – can be called. So, you could get Shairport Sync to ignore volume control information but call an executable to control an external volume control.
## Other Settings
* The `disable_standby_mode` setting can be used to prevent the DAC from transitioning between active and standby states, which can sometimes cause a faint popping noise. If activated, Shairport Sync sends frames of silence to keep the DAC busy, preventing it from entering standby mode.
* The `disable_synchronisation` setting can be used to prevent Shairport Sync from doing any interpolation whatever. Normally, this would lead to a problem when the DAC's buffer either overflowed or underflowed, but some very high-end streamers adjust the rate at which their DACs run to match the exact rate at which audio arrives, thus preventing underflow or overflow.
