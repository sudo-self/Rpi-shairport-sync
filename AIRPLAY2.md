AirPlay 2
===

## General
* **AirPlay 2 support is experimental and incomplete.** The focus of the development effort has been on getting the best possible audio performance. Many features outside that focus are missing or broken. So, for example, integration with Apple's Home app is missing; remote control doesn't work.
* For AirPlay 2, Shairport Sync requires the services of another application called [`nqptp`](https://github.com/mikebrady/nqptp) ("Not Quite PTP") for timing and synchronisation. `nqptp` must run as `root` and must have exclusive access to ports `319` and `320`.
* AirPlay 2 support is only available on recent Linux and FreeBSD builds. The FreeBSD implementation has not been extensively tested. OpenBSD and Cygwin are not supported. AirPlay 2 support will definitely not work on Mac OS X. The reason is that Shairport Sync needs to use [`nqptp`](https://github.com/mikebrady/nqptp), which uses ports `319` and `320`. These ports are already used by Mac OS X to support its implementation of AirPlay 2.
* The AirPlay 2 build requires a good deal of extra library support and may not fit into smaller devices. It also requires more CPU power and more RAM.

### What Works
- AirPlay 2 for iOS and Mac players.

### What Does Not Work
- No AirPlay 2 for Windows iTunes
- No integration with Siri or HomeKit
- Lossless or High Definition Lossless material seems to be transcoded to AAC before transmission over AirPlay 2. 
- No Remote Control
- No Artwork in the Metadata

### What Partly Works
- Incomplete functionality using AppleTV or HomePod as player.
   A Shairport Sync player will work -- i.e. it will play, but the selection button won't persist and the volume can't be set.

### Limitations
- You can only run a single instance of Shairport Sync on a device.
- The AirPlay 2 version of Shairport Sync will not run on macOS, since `nqptp` can not be installed on it.

### Known Issues
- A crash can occur occasionally when a Shairport Sync player is being selected for output. If you can get this to happen reliably, please let us know.

AirPlay 2 -- What You Need
---
AirPlay 2 support needs a more powerful CPU for decoding and synchronisation and more memory for bigger buffers and larger libraries. Raspberry Pi OS, Ubuntu 20.04 on a VM and Ubuntu 20.04.2 64-bit Server Edition have been used extensively in development, with Alpine Linux and FreeBSD 12.2 being used to a lesser extent.

Here are some suggestions: 
* Full access, including `root` privileges, to a system at least as powerful as a Raspberry Pi 3.
* A fully up-to-date Linux. This is important, as some of the libraries must be the latest available.
* An audio output, for example an `alsa` device (or `sndio` in FreeBSD). The `stdout` and `pipe` backends continue to work as before. Other backends have not been tested.

Guides
---
* To build Shairport Sync for AirPlay 2 on Linux, please follow the guide at [BUILDFORAP2.md](https://github.com/aillwee/shairport-sync/blob/development/BUILDFORAP2.md).
* A guide for building on FreeBSD is forthcoming.

AirPlay 2 -- More About What Works
---
* Two types of audio are received -- "Realtime" streams of CD quality ALAC (like AirPlay 1) and "Buffered Audio" streams of AAC stereo at 44,100 frames per second. The selection of stream type is made by the player.
* Audio is synchronised with other AirPlay 2 devices, including AirPlay 2 devices that have their own master clocks. (Note: This has not been tested with multiple nearly-identical master clock devices such as with two HomePod minis -- Shairport Sync will get confused about which is the current master.)
* Shairport Sync continues to support AirPlay 1, and offers an AirPlay 1 compatibility mode for situations where iTunes on macOS or macOS Music plays to multiple speakers and one of more of them is compatible with AirPlay 1 only.

Acknowledgements
----
Much of Shairport Sync's AirPlay 2 functionality is based on ideas developed at the [openairplay airplay2-receiver]( https://github.com/openairplay/airplay2-receiver) repository.

Big thanks are due to number of individuals made direct and valuable contributions to Shairport Sync:
1. [ejurgensen](https://github.com/ejurgensen) contributed ideas and code. The `pair_ap` submodule and related code in `rtsp.c` is theirs.
2. [JD Smith](https://github.com/jdtsmith) contributed ideas, comments and suggestions along with extensive and thorough testing.
3. [Charles Omer](https://github.com/charlesomer) contributed ideas, comments, bug fixes and Docker automation (forthcoming).

Finally
----
Did we mention AirPlay 2 support is experimental?
