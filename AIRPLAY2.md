AirPlay 2
===

Shairport Sync offers limited AirPlay 2 support for audio sources on iOS devices and Macs. In addition, it partly works if the audio source is a HomePod mini or an Apple TV. It does not work for iTunes on Windows. It has limited integration with the Home app and Siri.

The focus of the development effort has been on getting good audio performance for iOS and Mac. Features outside this focus may be missing or broken. So, for example, remote control doesn't work.

For AirPlay 2, Shairport Sync uses another application called [`nqptp`](https://github.com/mikebrady/nqptp) ("Not Quite PTP") for timing and synchronisation. `nqptp` must run as `root` and must have exclusive access to ports `319` and `320`.

AirPlay 2 functionality is available on recent Linux and FreeBSD builds. The FreeBSD implementation has not been extensively tested. OpenBSD and Cygwin are not supported.

The AirPlay 2 version of Shairport Sync will not work if installed on macOS. This is because [`nqptp`](https://github.com/mikebrady/nqptp) uses ports `319` and `320` but these ports are unavailable because there are used by macOS to support its implementation of AirPlay 2.

The AirPlay 2 build requires a good deal of extra library support and may not fit into smaller devices. It also requires more CPU power and more RAM.

### What Works
- AirPlay 2 for iOS and Mac players.

### What Does Not Work
- Lossless or High Definition Lossless material is transcoded to AAC before transmission over AirPlay 2. 
- No Remote Control
- No Artwork in the Metadata
- No AirPlay 2 for Windows iTunes

### What Partly Works
- Incomplete functionality with Siri and HomeKit
- Incomplete functionality using AppleTV or HomePod as player.
   A Shairport Sync player will work -- i.e. it will play -- but the selection button won't persist and the volume can't be set.

### Limitations
- Only a single instance of Shairport Sync can run on a device.
- The AirPlay 2 version of Shairport Sync will not work if installed on macOS, since `nqptp` can not be installed on macOS.

### Known Issues
- A crash can occur occasionally when a Shairport Sync player is being selected for output. If you can get this to happen reliably, please let us know.

AirPlay 2 -- What You Need
---
AirPlay 2 support needs a more powerful CPU for decoding and synchronisation and more memory for bigger buffers and larger libraries. Raspberry Pi OS, Ubuntu 20.04 on a VM and Ubuntu 20.04.2 64-bit Server Edition have been used extensively in development, with Alpine Linux and FreeBSD 12.2 being used to a lesser extent.

So, here are some guideline requirements: 
* Full access, including `root` privileges, to a system at least as powerful as a Raspberry Pi 3.
* A fully up-to-date Linux. This is important, as some of the libraries must be the latest available.
* An audio output, for example an `alsa` device (or `sndio` in FreeBSD). The `stdout` and `pipe` backends continue to work as before. Other backends have not been tested.

Guides
---
* To build Shairport Sync for AirPlay 2 on Linux, please follow the guide at [BUILDFORAP2.md](https://github.com/mikebrady/shairport-sync/blob/development/BUILDFORAP2.md).
* A guide for building on FreeBSD is forthcoming.

AirPlay 2 -- More About What Works
---
* Two types of audio are received by Shairport Sync -- "Realtime" streams of CD quality ALAC (like AirPlay 1) and "Buffered Audio" streams of AAC stereo at 44,100 frames per second. The selection of stream type is made by the player.
* Audio is synchronised with other AirPlay 2 devices, including AirPlay 2 devices that have their own master clocks.
* Shairport Sync continues to support AirPlay 1, and offers an AirPlay 1 compatibility mode for situations where iTunes on macOS or macOS Music plays to multiple speakers and one of more of them is compatible with AirPlay 1 only.

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
Did we mention AirPlay 2 support is experimental?
