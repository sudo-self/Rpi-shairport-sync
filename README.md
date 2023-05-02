# Shairport Sync
## Shairport Sync does not support AirPlay video or photo streaming.

# Quick Start
## apt install --no-install-recommends build-essential git autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev \
    libplist-dev libsodium-dev libavutil-dev libavcodec-dev libavformat-dev uuid-dev libgcrypt-dev xxd
## systemctl enable avahi-daemon
## systemctl start avahi-daemon
## apt install --no-install-recommends build-essential git autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev \
    libplist-dev libsodium-dev libavutil-dev libavcodec-dev libavformat-dev uuid-dev libgcrypt-dev xxd
## git clone https://github.com/mikebrady/shairport-sync.git
## cd shairport-sync
## autoreconf -fi
##  ./configure --sysconfdir=/etc --with-alsa \
>     --with-soxr --with-avahi --with-ssl=openssl --with-systemd --with-airplay-2
## make && make install
## git clone https://github.com/mikebrady/nqptp.git && cd nqptp 
## autoreconf -fi
## ./configure --with-systemd-startup
## make && make install 
## systemctl start nqptp
## systemctl restart shairport-sync
## shairport-sync
## iwconfig wlan0 power off
## shairport-sync -d
## shairport-sync -v
## systemctl enable shairport-sync
## sudo reboot



* A building guide is available [here](BUILD.md).
* Next Steps and Advanced Topics are [here](ADVANCED%20TOPICS/README.md).
![2](https://user-images.githubusercontent.com/119916323/235680254-d8875bc7-0cd4-47d0-b0e3-081d8ed331af.jpg)
![1](https://user-images.githubusercontent.com/119916323/235680066-75291086-8d8a-46bd-80e9-25e32671621c.JPG)


# Status
Shairport Sync was designed to [run best](ADVANCED%20TOPICS/GetTheBest.md) on stable, dedicated, stand-alone low-power "headless" systems with ALSA as the audio system and with a decent CD-quality Digital to Analog Converter (DAC).

Shairport Sync runs on recent (2018 onwards) Linux systems and FreeBSD from 12.1 onwards. It requires a system with the power of a Raspberry Pi 2 or a Pi Zero 2 or better.

Classic Shairport Sync runs on a wider variety of Linux sytems, including OpenWrt and Cygwin and it also runs on OpenBSD. Many embedded devices are powerful enough to power classic Shairport Sync.



## Synchronised Audio
Shairport Sync offers *full audio synchronisation*. Full audio synchronisation means that audio is played on the output device at exactly the time specified by the audio source. To accomplish this, Shairport Sync needs access to audio systems – such as ALSA on Linux and `sndio` on FreeBSD – that provide very accurate timing information about audio being streamed to output devices. Ideally, Shairport Sync should have direct access to the output device used, which should be a real sound card capable of working with 44,100, 88,200 or 176,400 samples per second, interleaved PCM stereo of 8, 16, 24 or 32 bits. Using the ALSA sound system, Shairport Sync will choose the greatest bit depth available at 44,100 samples per second, resorting to multiples of 44,100 if it is not available. You'll get a message in the log if there's a problem. With all other sound systems, a sample rate of 44,100 is chosen with a bit depth of 16 bit.

Shairport Sync works well with PulseAudio, a widely used sound server found on many desktop Linuxes. While the timing information is not as accurate as that of ALSA or `sndio`, it is often impractical to remove or disable PulseAudio. 

For other use cases, Shairport Sync can provide synchronised audio output to a unix pipe or to `STDOUT`, or to audio systems that do not provide timing information. This could perhaps be described as *partial audio synchronisation*, where synchronised audio is provided by Shairport Sync, but what happens to it in the subsequent processing chain, before it reaches the listener's ear, is outside the control of Shairport Sync.

## Latency, "Stuffing", Timing
AirPlay protocols use an agreed *latency* – a time lag or delay – between the time represented by a sound sample's `timestamp` and the time it is actually played by the audio output device, typically a Digital to Audio Converter (DAC). Latency gives players time to compensate for network delays, processing time variations and so on. The latency is specified by the audio source when it negotiates with Shairport Sync. AirPlay sources set a latency of around 2.0 to 2.25 seconds. AirPlay 2 can use shorter latencies, around half a second.

As mentioned previously, Shairport Sync implements full audio synchronisation when used with ALSA, `sndio` or PulseAudio audio systems. This is done by monitoring the timestamps present in data coming from the audio source and the timing information coming back from the audio system itself. To maintain the  latency required for exact synchronisation, if the output device is running slow relative to the source, Shairport Sync will delete frames of audio to allow the device to keep up. If the output device is running fast, Shairport Sync will insert ("stuff") extra frames to keep time. The number of frames inserted or deleted is so small as to be almost inaudible on normal audio material. Frames are inserted or deleted as necessary at pseudorandom intervals. Alternatively, with `libsoxr` support, Shairport Sync can resample the audio feed to ensure the output device can keep up. This is less obtrusive than insertion and deletion but requires a good deal of processing power — most embedded devices probably can't support it. If your computer is fast enough, Shairport Sync will, by default, automatically choose this method.

Stuffing is not done for partial audio synchronisation – the audio samples are simply presented at exactly the right time to the next stage in the processing chain.

Timestamps are referenced relative to the source computer's clock – the "source clock", but timing must be done relative to the clock of the computer running Shairport Sync – the "local clock". So, Shairport Sync synchronises the source clock and the local clock, usually to within a fraction of a millisecond. In AirPlay 2, this is done with the assistance of a companion application called [NQPTP](https://github.com/mikebrady/nqptp) using a [PTP](https://en.wikipedia.org/wiki/Precision_Time_Protocol)-based timing protocol. In classic AirPlay, a variant of [NTP](https://en.wikipedia.org/wiki/Network_Time_Protocol) synchronisation protocols is used.
