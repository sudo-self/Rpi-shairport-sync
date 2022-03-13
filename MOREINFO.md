More Information
----------
Shairport Sync offers *full audio synchronisation*, a feature of AirPlay that previous implementations do not provide. Full audio synchronisation means that audio is played on the output device at exactly the time specified by the audio source. To accomplish this, Shairport Sync needs access to audio systems – such as `alsa` on Linux and `sndio` on FreeBSD – that provide very accurate timing information about output devices. Shairport Sync must have direct access to the output device used, which must be a real sound card capable of working with 44,100, 88,200 or 176,400 samples per second, interleaved PCM stereo of 8, 16, 24 or 32 bits. The default is 44,100 samples per second / 16 bits (you'll get a message in the logfile if there's a problem).

Alternatively, Shairport Sync works well with PulseAudio, a widely used sound server found on many desktop Linuxes. While the timing information is not as accurate as that of `alsa` or `sndio`, it is often impractical to remove or disable PulseAudio. In that case, the `pa` backend can be used. 

For other use cases, Shairport Sync can provide synchronised audio output to a unix pipe or to standard output, or to audio systems that do not provide timing information. This could perhaps be described as *partial audio synchronisation*, where synchronised audio is provided by Shairport Sync, but what happens to it in the subsequent processing chain, before it reaches the listener's ear, is outside the control of Shairport Sync.

For more about the motivation behind Shairport Sync, please see the wiki at https://github.com/mikebrady/shairport-sync/wiki.

Synchronisation, Latency, "Stuffing"
---------
The AirPlay protocol uses an agreed *latency* – the time difference, or delay, between the time represented by a sound sample's `timestamp` and the time it is actually played by the audio output device, typically a Digital to Audio Converter (DAC). The latency to be used is specified by the audio source when it negotiates with Shairport Sync.

As mentioned previously, Shairport Sync implements full audio synchronisation when used with `alsa`, `sndio` or PulseAudio systems. This is done by monitoring the timestamps present in data coming from the audio source and the timing information from the audio system, e.g. `alsa`. To maintain the  latency required for exact synchronisation, if the output device is running slow relative to the source, Shairport Sync will delete frames of audio to allow the device to keep up. If the output device is running fast, Shairport Sync will insert frames to keep time. The number of frames inserted or deleted is so small as to be almost inaudible on normal audio material. Frames are inserted or deleted as necessary at pseudorandom intervals. Alternatively, with `libsoxr` support, Shairport Sync can resample the audio feed to ensure the output device can keep up. This is less obtrusive than insertion and deletion but requires a good deal of processing power — most embedded devices probably can't support it. The process of insertion/deletion or resampling is rather inelegantly called “stuffing”.

Stuffing is not done for partial audio synchronisation – the audio samples are simply presented at exactly the right time to the next stage in the processing chain.

Timestamps are referenced relative to the source computer's clock – the `source clock`, but timing must be done relative to the clock of the computer running Shairport Sync – the `local clock`. So, another thing Shairport Sync has to do is to synchronize the source clock and the local clock, and it does this usually to within a fraction of a millisecond, using either the PTP or NTP synchronisation protocols.

What else?
--------------
* Better Volume Control — Shairport Sync offers finer control at very top and very bottom of the volume range. See http://tangentsoft.net/audio/atten.html for a good discussion of audio "attenuators", upon which volume control in Shairport Sync is modelled. See also the diagram of the volume transfer function in the documents folder. In addition, Shairport Sync can offer an extended volume control range on devices with a restricted range.
* Hardware Mute — Shairport Sync can mute properly if the hardware supports it.
* Support for the Apple ALAC decoder.
* Output bit depths of 8, 16, 24 and 32 bits, rather than the standard 16 bits.
* Output frame rates of 44,100, 88,200, 176,000 or 352,000 frames per second.
* Fast Response — With hardware volume control, response is instantaneous; otherwise the response time is 0.20 seconds with `alsa`, 0.35 seconds with `sndio`.
* Metadata — Shairport Sync can deliver metadata supplied by the source, such as Album Name, Artist Name, Cover Art, etc. through a pipe or UDP socket to a recipient application program — see https://github.com/mikebrady/shairport-sync-metadata-reader for a sample recipient. Sources that supply metadata include iTunes and the Music app in macOS and iOS.
* Compiles on Linux and FreeBSD.
* Outputs to [`alsa`](https://www.alsa-project.org/wiki/Main_Page), [`sndio`](http://www.sndio.org), [PulseAudio](https://www.freedesktop.org/wiki/Software/PulseAudio/), [JACK](http://jackaudio.org), to a unix pipe or to `STDOUT`. It also has limited support for [libao](https://xiph.org/ao/) and for [`soundio`](http://libsound.io).
* An [MPRIS](https://specifications.freedesktop.org/mpris-spec/2.2/) interface, partially complete and very functional, including access to metadata and artwork, and some limited remote control.
* An interface to [MQTT](https://en.wikipedia.org/wiki/MQTT), an often-used protocol in home automation projects.
* A native D-Bus interface, including access to metadata and artwork, some limited remote control and some system settings.

Heritage
-------
Shairport Sync is a substantial rewrite of the fantastic work done in Shairport 1.0 by James Laird and others — please see https://github.com/abrasive/shairport/blob/master/README.md#contributors-to-version-1x for a list of the contributors to Shairport 1.x and Shairport 0.x. From a "heritage" point of view, Shairport Sync is a fork of Shairport 1.0.


Configuring Shairport Sync
--------
To get the best from Shairport Sync, you’ll need to (a) give Shairport Sync a service name by which it will be seen in iTunes etc. and (b) specify the backend you wish to use - `alsa` for the ALSA backend, or `pa` for the PulseAudio back end, etc. If only one backend is included at compilation, or if the backend is ALSA, there is no need to explicitly specify the backend.

For the ALSA backend you may need to (c) specify the output device to use and (d) specify the name of the mixer volume control to use to control the output level. To get values for (c) and (d) you might need to explore the ALSA output devices with a program like `alsamixer` or similar.

Shairport Sync reads settings from a configuration file at `/etc/shairport-sync.conf` (note that in FreeBSD it will be at `/usr/local/etc/shairport-sync.conf`). When you run `$sudo make install`, a sample configuration file is installed or updated at `/etc/shairport-sync.conf.sample` (`/usr/local/etc/shairport-sync.conf.sample` in FreeBSD). This contains all the setting groups and all the settings available, but they all are commented out (comments begin with `//`) so that default values are used. The file contains explanations of the settings, useful hints and suggestions. In addition, if the file doesn't already exist, a default configuration is installed, which should work in almost any system with a sound card.

Settings in the configuration file are grouped. For instance, there is a `general` group within which you can use the `name` tag to set the service name. Suppose you wanted to set the name of the service to `Front Room` asd give the service the password `secret`, then you should do the following:

```
general =
{
  name = "Front Room";
  password = "secret";
  // ... other general settings
};
```
(Remember, anything preceded by `//` is a comment and will have no effect on the setting of Shairport Sync.)

**Important:** You should *never* use an important password as the AirPlay password for a Shairport Sync player -- the password is stored in Shairport Sync's configuration file in plain text and is thus completely vulnerable.

No backend is specified here, so it will default to the `alsa` backend if more than one back end has been compiled. To route the output to PulseAudio, add:

```
  output_backend = "pa";
```
to the `general` group.

The `alsa` group is used to specify properties of the output device. The most obvious setting is the name of the output device which you can set using the `output_device` tag.

The following `alsa` group settings are very important for maximum performance. If your audio device has a mixer that can be use to control the volume, then Shairport Sync can use it to give instant response to volume and mute commands and it can offload some work from the processor.
* The `mixer_control_name` tag allows you to specify the name of the mixer volume control.
* The `mixer_device` tag allows you specify where the mixer is. By default, the mixer is on the `output_device`, so you only need to use the `mixer_device` tag if the mixer is elsewhere. This can happen if you specify a *device* rather than a *card* with the `output_device` tag, because normally a mixer is associated with a *card* rather than a device. Suppose you wish to use the output device `5` of card `hw:0` and the mixer volume-control named `PCM`:

```
alsa =
{
  output_device = "hw:0,5";
  mixer_device = "hw:0";
  mixer_control_name = "PCM";
  // ... other alsa settings
};
```

The `pa` group is used to specify settings relevant to the PulseAudio backend. You can set the "Application Name" that will appear in the "Sound" control panel.

Shairport Sync can run programs just before it starts to play an audio stream and just after it finishes. You specify them using the `sessioncontrol` group settings `run_this_before_play_begins` and `run_this_after_play_ends`. This is to facilitate situations where something has to be done before and after playing, e.g. switching on an amplifier beforehand and switching it off afterwards. Set the `wait_for_completion` value to `"yes"` for Shairport Sync to wait until the respective commands have been completed before continuing.

Note that the full path to the programs must be specified, and script files will not be executed unless they are marked as executable and have the appropriate shebang `#!/bin/...` as the first line. (This behaviour may be different from other Shairports.)

Shairport Sync can run a program whenever the volume is set or changed. You specify it using the `general` group setting `run_this_when_volume_changes`. This is to facilitate situations where something has to be done when the volume changes, e.g. adjust an external mixer value. Set the `wait_for_completion` value to `"yes"` for Shairport Sync to wait until the command has been completed before continuing. Again, please note that the full path to the program must be specified, and script files will not be executed unless they are marked as executable and have the appropriate shebang `#!/bin/...` as the first line.

Note: Shairport Sync can take configuration settings from command line options. This is mainly for backward compatibility, but sometimes still useful. For normal use, it is strongly recommended that you use the configuration file method.

**Raspberry Pi**

Many Raspberry Pi Models have a built-in audio DAC that is connected to the device's headphone jack. Apart from a loud click when used for the first time after power-up, it is now quite adequate for casual listening.

To make Shairport Sync output to the built-in audio DAC and use its hardware mixer, in the `alsa` section of the configuration file, set the output device and mixer as follows:
```
alsa =
{
  output_device = "hw:0"; // the name of the alsa output device. Use "alsamixer" or "aplay" to find out the names of devices, mixers, etc.
  mixer_control_name = "Headphone"; // the name of the mixer to use to adjust output volume. If not specified, volume in adjusted in software.
  // ... other alsa settings
```
(Remember to uncomment the lines by removing the `//` at the start of each.) When these changes have been made, reboot the machine.

A problem with the built-in DAC that it declares itself to have a very large mixer volume control range – all the way from -102.38dB up to +4dB, a range of 106.38 dB. In reality, only the top 60 dB of it is in any way usable. To help get the most from it, consider using the `volume_range_db` setting in the `general` group to instruct Shairport Sync to use the top of the DAC mixer's declared range. For example, if you set the `volume_range_db` figure to 60, the top 60 dB of the range will the used. With this setting on the Raspberry Pi, maximum volume will be +4dB and minimum volume will be -56dB, below which muting will occur.

From a user's point of view, the effect of using this setting is to move the minimum usable volume all the way down to the bottom of the user's volume control, rather than have the minimum usable volume concentrated very close to the maximum volume.

Examples
--------

Here are some examples of complete configuration files.

```
general = {
  name = "Joe's Stereo";
};

alsa = {
  output_device = "hw:0";
};
```

This gives the service a particular name — "Joe's Stereo" and specifies that audio device hw:0 be used.

For best results with the `alsa` backend — including getting true mute and instant response to volume control and pause commands — you should access the hardware volume controls. Use `amixer` or `alsamixer` or similar to discover the name of the mixer control to be used as the `mixer_control_name`.

Here is an example for for a Raspberry Pi using its internal soundcard — device hw:0 — that drives the headphone jack:
```
general = {
  name = "Mike's Boombox";
};

alsa = {
  output_device = "hw:0";
  mixer_control_name = "Headphone";
};
```

Here is an example of driving a Topping TP30 Digital Amplifier, which has an integrated USB DAC and which is connected as audio device `hw:1`:
```
general = {
  name = "Kitchen";
};

alsa = {
  output_device = "hw:1";
  mixer_control_name = "PCM";
};
```

For a cheapo "3D Sound" USB card (Stereo output and input only) on a Raspberry Pi:
```
general = {
  name = "Front Room";
};

alsa = {
  output_device = "hw:1";
  mixer_control_name = "Speaker";
};
```


Finally, here is an example of using the PulseAudio backend:
```
general = {
  name = "Zoe's Computer";
  output_backend = "pa";
};

```


Metadata
--------

Shairport Sync can deliver metadata supplied by the source, such as Album Name, Artist Name, Cover Art, etc. through a pipe or UDP socket to a recipient application program — see https://github.com/mikebrady/shairport-sync-metadata-reader for a sample recipient. Sources that supply metadata include iTunes and the Music app in macOS and iOS.

**Metadata broadcasting over UDP**

As an alternative to sending metadata to a pipe, the `socket_address` and `socket_port` tags may be set in the metadata group to cause Shairport Sync to broadcast UDP packets containing the track metadata.

The advantage of UDP is that packets can be sent to a single listener or, if a multicast address is used, to multiple listeners. It also allows metadata to be routed to a different host. However UDP has a maximum packet size of about 65000 bytes; while large enough for most data, Cover Art will often exceed this value. Any metadata exceeding this limit will not be sent over the socket interface. The maximum packet size may be set with the `socket_msglength` tag to any value between 500 and 65000 to control this - lower values may be used to ensure that each UDP packet is sent in a single network frame. The default is 500. Other than this restriction, metadata sent over the socket interface is identical to metadata sent over the pipe interface.

The UDP metadata format is very simple - the first four bytes are the metadata *type*, and the next four bytes are the metadata *code* (both are sent in network byte order - see https://github.com/mikebrady/shairport-sync-metadata-reader for a definition of those terms). The remaining bytes of the packet, if any, make up the raw value of the metadata.

Latency
-------
Latency is the exact time from a sound signal's original timestamp until that signal actually "appears" on the output of the audio output device, usually a Digital to Audio Converter (DAC), irrespective of any internal delays, processing times, etc. in the computer.

Problems can arise when you are trying to synchronise with speaker systems — typically surround-sound home theatre systems — that have their own inherent delays. You can compensate for an inherent delay using the appropriate backend (typically `alsa`) `audio_backend_latency_offset_in_seconds`. Set this offset to compensate for a fixed delay in the audio back end; for example, if the output device delays by 100 ms, set this to -0.1.

Resynchronisation
-------------
Shairport Sync actively maintains synchronisation with the source.
If synchronisation is lost — say due to a busy source or a congested network — Shairport Sync will mute its output and resynchronise. The loss-of-sync threshold is a very conservative 0.050 seconds — i.e. the actual time and the expected time must differ by more than 50 ms to trigger a resynchronisation. Smaller disparities are corrected by insertions or deletions, as described above.
* You can vary the resync threshold, or turn resync off completely, with the `general` `resync_threshold_in_seconds` setting.

Tolerance
---------
Playback synchronisation is allowed to wander — to "drift" — a small amount before attempting to correct it. The default is 0.002 seconds, i.e. 2 ms. The smaller the tolerance, the  more  likely it is that overcorrection will occur. Overcorrection is when more corrections (insertions and deletions) are made than are strictly necessary to keep the stream in sync. Use the `statistics` setting to monitor correction levels. Corrections should not greatly exceed net corrections.
* You can vary the tolerance with the `general` `drift_tolerance_in_seconds` setting.

Statistics
---------------
If you set the `statistics` setting in the `diagnostics` section of the configuration file to `"YES"`, some statistics will be logged at regular intervals. The items logged will depend on the type of stream being processed: AirPlay 1, AirPlay 2 Buffered Audio or AirPlay 2 Realtime Audio. If the `log_verbosity` is set to 1, 2 or 3, additional items will be logged.

From an audio enthusiast's point of view, the most important figure is possibly the `all sync ppm` figure. This is the total amount of interpolation needed by Shairport Sync to keep the audio stream in sync. The number represents is the ratio of frames added and removed from the audio stream relative to all the frames output in the last interval, expressed in parts per million (ppm). For reference, adding or removing one frame per second into a 44,100 frames per second stream is ± 22.68 ppm. The lower this number number is, the higher the fidelity of the audio signal passed to the output device. On a well sorted system, this figure can be 0.0 for considerable periods, but it can't really be zero forever. You may also find that the number might be higher at the start while the system settles down.

The second most important figure is possibly the `sync error ms`. This is the average synchronisation error in milliseconds in the last interval. Ideally it should be 0.0. By default, Shairport Sync has a tolerance of a sync error of ± 2.0 milliseconds without triggering interpolation. 

Two other interesting measurements of the output rate may be available -- `output fps (r)` and `output fps (c)`, where `(r)` means "raw" and the `(c)` means "corrected".

The "raw" figure is the rate at which the output device (typically a DAC) accepts data measured relative to the computer's own system clock (specifically the `CLOCK_MONOTONIC_RAW` clock). The accuracy of the number depends on the accuracy of the clock, which will typically be accurate to within anything from 20 to 100 ppm.

The "corrected" figure is the rate at which the output device accepts data relative to the computer's network-time-disciplined clock (specifically the `CLOCK_MONOTONIC` clock). This clock is normally adjusted ("disciplined") to keep time with network time and should be accurate to with a few tens of milliseconds over a long period. So (1) if you could run a play session for a long period -- say a day -- and (2) if network time synchronisation is enabled and (3) if the network connection to the network time service is fast and stable, then you should get an accurate absolute measure of exact frame rate of the output device. If your internet connection is not good, the corrected figure will be very inaccurate indeed.

Here is a brief description of the figures that might be provided.
##### sync error ms
Average playback synchronisation error in milliseconds in the last interval. By default, Shairport Sync will allow a sync error of ± 2.0 milliseconds without any interpolation. Positive means late, negative means early.
##### net sync ppm
This is the total amount of interpolation done by Shairport Sync to keep the audio stream in sync. The number represents is the total number of frames added and removed from the audio stream, expressed in parts per million (ppm) in the last interval. For reference, adding or removing one frame per second into a 44,100 frames per second stream is 22.68 ppm. 
##### all sync ppm
This is the net amount of interpolation done by Shairport Sync to keep the audio stream in sync. The number represents is the number of frames added plus the number removed from the audio stream, expressed in parts per million (ppm) in the last interval. The magnitude of this should be the same as the `net sync ppm'. If it is much larger it means that Shairport Sync is overcorrecting for sync errors -- try increasing the drift tolerance to reduce it.
##### packets
This is the number of packets of audio frames received since the start of the session. A packet normally contains 352 ± 1 audio frames.
##### missing
This is the number of packets of audio frames that were not received in the last interval. It should be zero, and if not it can indicate a problem with the network. AirPlay 1 and AirPlay 2 Realtime Streams only.
##### late
This is the number of packets of audio frames that were received late -- but still in time to be used -- in the last interval. AirPlay 1 and AirPlay 2 Realtime Streams only.
##### too late
This is the number of packets of audio frames that were received too late to be used in the last interval. It is possible that these packets may already have been received, so those frames might not actually be missing when the time comes to play them. AirPlay 1 and AirPlay 2 Realtime Streams only.
##### resend reqs
This is the number of times Shairport Sync requests the resending of missing frames. Requests can be for one or more frames. AirPlay 1 and AirPlay 2 Realtime Streams only.
##### min DAC queue
The is the smallest number of frames of audio in the DAC's hardware queue. If it goes too low, the DAC may begin to underrun.
##### min buffers
The is the smallest number of packets of audio in the queue to be processed in the last interval. It is related to the overall latency in AirPlay 1 and AirPlay 2 Realtime Streams. If it comes close to zero it's often a sign that the network is poor.
##### max buffers
The is the largest number of packets of audio in the queue to be processed in the last interval. 
##### min buffer size
The is smallest remaining number of bytes in the Buffered Audio buffer in the last interval. It can legitimately be zero when a track ends or begins. If it reaches zero while a track is playing, it means that audio data is not arriving at Shairport Sync quickly enough and may indicate network problems. AirPlay 2 Buffered Audio streams only.
##### nominal fps
This is the rate specified in the AirPlay stream itself. AirPlay 1 only.
##### received fps
This is the rate at which frames are received from the network averaged since the start of the play session. AirPlay 1 only.
##### output fps (r)
Output rate measured relative to the computer system's clock since the start of the play session. See above for a discussion.
##### output fps (c)
Output rate measured relative to the network-clock-disciplined computer system's clock since the start of the play session. See above for a discussion.
##### source drift ppm
This is a measure of the difference between the source clock and Shairport Sync's clock expressed in parts per million. Only valid when 10 or more drift samples have been received. AirPlay 1 only.
##### drift samples
This is the number drift samples have been accepted for calculating the source drift. AirPlay 1 only.
#### Example
The following example is of an AirPlay 2 Buffered Audio Stream from a HomePod mini to a WiFi-connected Raspberry Pi 3 equipped with a Pimoroni "Audio DAC SHIM (Line-Out)" with `log_verbosity` set to `1`.

```
 sync error ms net sync ppm all sync ppm     packets min DAC queue min buffers max buffers output fps (r) output fps (c)
         -1.88          8.5          8.5      526575          8455          62          63       44099.98       44099.92
         -1.92         31.2         31.2      527578          8460          62          63       44099.98       44099.92
         -1.87          2.8          2.8      528581          8467          62          63       44099.98       44099.93
         -1.98         22.7         22.7      529584          8466          62          63       44099.98       44099.93
         -2.01         73.6         73.6      530587          8468          62          63       44099.98       44099.93
         -1.99         93.5         93.5      531590          8463          62          62       44099.98       44099.94
         -1.77          5.7          5.7      532593          8466          62          63       44099.98       44099.94
         -1.84          2.8          2.8      533596          8456          62          62       44099.98       44099.95
         -1.54          0.0          0.0      534599          8477          62          63       44099.98       44099.95
         -1.19          0.0          0.0      535602          8501          62          62       44099.98       44099.95
         -1.03          0.0          0.0      536605          8505          62          62       44099.98       44099.96
         -1.10          0.0          0.0      537608          8497          62          63       44099.98       44099.96
         -1.39          0.0          0.0      538611          8485          62          63       44099.98       44099.97
         -1.43          0.0          0.0      539614          8485          62          63       44099.98       44099.97
         -1.75          0.0          0.0      540617          8481          62          62       44099.98       44099.97
         -1.52          0.0          0.0      541620          8474          62          63       44099.98       44099.98
         -1.37          0.0          0.0      542623          8488          62          62       44099.98       44099.98
         -1.63          0.0          0.0      543626          8481          62          62       44099.98       44099.98
         -2.01        102.0        102.0      544629          8465          62          62       44099.98       44099.99
         -1.88         14.2         14.2      545632          8474          62          62       44099.98       44099.99
         -1.42          0.0          0.0      546635          8486          62          63       44099.98       44099.99
         -1.92         17.0         17.0      547638          8435          62          63       44099.98       44100.00
         -1.27          0.0          0.0      548641          8479          62          62       44099.98       44100.00
         -0.64          0.0          0.0      549644          8528          62          62       44099.98       44100.00
         -1.12          0.0          0.0      550647          8476          62          62       44099.98       44100.01
         -1.88         25.5         25.5      551650          8471          62          62       44099.98       44100.01
         -1.79          0.0          0.0      552653          8477          62          62       44099.98       44100.01
         -1.79          0.0          0.0      553656          8477          62          63       44099.98       44100.02
         -1.81         19.8         19.8      554659          8467          62          62       44099.98       44100.02
         -2.04        110.5        110.5      555662          8468          62          63       44099.98       44100.02
         -1.64          2.8          2.8      556665          8476          62          62       44099.98       44100.02
         -1.29          0.0          0.0      557668          8496          62          63       44099.98       44100.03
         -1.20          0.0          0.0      558671          8500          62          63       44099.98       44100.03
         -1.33          0.0          0.0      559674          8483          62          63       44099.98       44100.03
         -1.43          0.0          0.0      560677          8468          62          63       44099.98       44100.03
         -1.47          0.0          0.0      561680          8484          62          62       44099.98       44100.04
         -0.88          0.0          0.0      562683          8512          62          62       44099.98       44100.04
         -1.16          0.0          0.0      563686          8500          62          63       44099.98       44100.04
         -1.42          0.0          0.0      564689          8488          62          62       44099.98       44100.05
         -1.58          0.0          0.0      565692          8476          62          63       44099.98       44100.05
         -1.50          0.0          0.0      566695          8488          62          63       44099.98       44100.05
         -1.79          0.0          0.0      567698          8471          62          63       44099.98       44100.05
         -1.82          8.5          8.5      568701          8473          62          62       44099.98       44100.06
         -1.87         19.8         19.8      569704          8468          62          63       44099.98       44100.06
         -1.96         85.0         85.0      570707          8456          62          63       44099.98       44100.06
         -1.80          2.8          2.8      571710          8452          62          63       44099.98       44100.06
         -1.66          0.0          0.0      572713          8477          62          62       44099.98       44100.07
```
For reference, a drift of one second per day is approximately 11.57 ppm. Left uncorrected, even a drift this small between two audio outputs will be audible after a short time.

WiFi Issues
---------
If you are using WiFi, you should ensure that WiFi power management is off. See [TROUBLESHOOTING](https://github.com/mikebrady/shairport-sync/blob/master/TROUBLESHOOTING.md) for more details.

Troubleshooting
---------------
Please refer to [TROUBLESHOOTING](https://github.com/mikebrady/shairport-sync/blob/master/TROUBLESHOOTING.md) for a few hints, contributed by users.

MQTT
---------------
Please refer to the [MQTT INFO](https://github.com/mikebrady/shairport-sync/blob/master/MQTT.md) page for additional info on building, configuring and using MQTT to interface shairport-sync with common home automation systems (contributed by users).

Packaging status
----------------

[![Packaging status](https://repology.org/badge/vertical-allrepos/shairport-sync.svg)](https://repology.org/project/shairport-sync/versions)

