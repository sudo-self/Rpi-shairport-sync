AirPlay 2
===
Shairport Sync offers AirPlay 2 support for audio sources on iOS devices, Macs, HomePod minis and Apple TVs.

### Features and Limitations
#### What Works
- AirPlay 2 for iOS, HomePod mini, AppleTV and Mac players.

#### What Does Not Work
- No Remote Control
- No AirPlay 2 for Windows iTunes

### General
Shairport Sync uses another application called [`nqptp`](https://github.com/mikebrady/nqptp) ("Not Quite PTP") for timing and synchronisation in AirPlay 2. `nqptp` must run as `root` and must have exclusive access to ports `319` and `320`.

Shairport Sync does not support lossless playback of Lossless or High Definition Lossless material. Instead, this material is automatically transcoded to AAC before sending it via AirPlay 2 to Shairport Sync. 

AirPlay 2 -- What You Need
---
AirPlay 2 support needs a slightly more powerful CPU for decoding and synchronisation and more memory for bigger buffers and larger libraries. A Raspberry Pi 2 or Raspberry Pi Zero 2 W or better is required.

Here are some guidelines: 
* Full access, including `root` privileges, to a system at least as powerful as a Raspberry Pi 2 or a Raspberry Pi Zero 2 W.
* A fully up-to-date Linux or FreeBSD. This is important, as some of the libraries must be the latest available.
* A version of the ffmpeg library with an AAC decoder capable of decoding Floating Planar -- `fltp` -- material. There is a guide [here](https://github.com/mikebrady/shairport-sync/blob/development/TROUBLESHOOTING.md#aac-decoder-issues-airplay-2-only) to help you find out. (At the time of writing, Debian/Ubuntu/Raspberry Pi OS and FreeBSD all include this decoder. Fedora 36 does not.)
* An audio output, for example an `alsa` device (or `sndio` in FreeBSD). You can use an application called [`sps-alsa-explore`](https://github.com/mikebrady/sps-alsa-explore) to test the suitability of hardware `alsa` audio devices on your device. Other backends continue to work as with "classic" Shairport Sync.

Guides
---
* To build Shairport Sync for AirPlay 2 on Linux, please follow the guide at [BUILDFORAP2.md](https://github.com/mikebrady/shairport-sync/blob/development/BUILDFORAP2.md).
* A guide for building on FreeBSD is forthcoming.

AirPlay 2 -- More About What Works
---
* Two types of audio are received by Shairport Sync -- "Realtime" streams of CD quality ALAC (like AirPlay 1) and "Buffered Audio" streams of AAC stereo at 44,100 frames per second. The selection of stream type is made by the player.
* Audio is synchronised with other AirPlay 2 devices, including AirPlay 2 devices that have their own master clocks.
* Shairport Sync offers AirPlay 1 compatibility for situations where iTunes on macOS or macOS Music plays to multiple speakers and where one of more of them is compatible with AirPlay 1 only.

Note
----
The functionality offered by Shairport Sync is the result of lots of study and analysis of the AirPlay 1 and AirPlay 2 protocols by many people over the years. These protocols have not been officially published, and there is no assurance that Shairport Sync will continue to work with AirPlay in future.

Acknowledgements
----
Huge thanks are due to a number of individuals who made direct and valuable contributions to Shairport Sync:
1. [ejurgensen](https://github.com/ejurgensen) contributed ideas and code. The `pair_ap` submodule and related code in `rtsp.c` is theirs.
2. [JD Smith](https://github.com/jdtsmith) contributed ideas, comments and suggestions along with extensive and thorough testing.
3. [Charles Omer](https://github.com/charlesomer) contributed ideas, comments, bug fixes, documentation and Docker automation (forthcoming).
4. [ckdo](https://github.com/ckdo) for their pathfinding work on AirPlay 2, especially the HomeKit pairing.

Much of Shairport Sync's AirPlay 2 functionality is based on ideas developed at the [openairplay airplay2-receiver]( https://github.com/openairplay/airplay2-receiver) repository. It is a pleasure to acknowledge the work of the contributors there.

Finally
----
AirPlay 2 support is experimental.
