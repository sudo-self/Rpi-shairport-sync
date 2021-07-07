AirPlay 2 Matters
====
* The AirPlay 2 build requires a good deal of extra library support and may not fit into smaller devices. It also requires more CPU power and more RAM -- the minimum recommended device is a Raspberry Pi 3.
* **AirPlay 2 support is experimental and incomplete.** The focus of the development effort has been on getting the best possible audio performance. Many features outside that focus are missing or broken. So, for example, integration with Apple's Home app is missing; remote control doesn't work.
* For AirPlay 2, Shairport Sync requires the services of another application called [`NQPTP`](https://github.com/mikebrady/nqptp) ("Not Quite PTP") for timing and synchronisation. NQPTP must run as `root` and must have exclusive access to ports `319` and `320`.
* AirPlay 2 support is only available on recent Linux and FreeBSD builds. The FreeBSD implementation has not been extensively tested. OpenBSD and Cygwin are not supported. AirPlay 2 support will definitely not work on Mac OS X. The reason is that Shairport Sync needs to use [`NQPTP`](https://github.com/mikebrady/nqptp), which uses ports `319` and `320`. These ports are already used by Mac OS X to support its implementation of AirPlay 2.

Did we mention AirPlay 2 support is experimental? You should also note that it is possible that changes could be made that would stop AirPlay 2 support from working. Let's hope that doesn't happen -- due to its limitations, there seems little prospect of it having any kind of commercial significance.

What Works
---
* AirPlay 2 audio for players hosted on macOS and iOS.
* AirPlay 2 audio for System Sound Output from macOS.
* The `ALSA`, `stdout` and `pipe` backends definitely work, as does the `sndio` backend for FreeBSD.

What Does Not Work
---
* Shairport Sync AirPlay 2 can not be used from a PC or from an Apple TV.
* There is no integration with Siri or with the Home app or HomeKit, so nothing to do with the Home app works.
* You can not run multiple instances of Shairport Sync on one device. This is because an instance of Shairport Sync requires exclusive access to [`NQPTP`](https://github.com/mikebrady/nqptp), and only one instance of that can run on a processor. (Docker may offer a workaround, but this has not been tested.)
* Remote Control from Shairport Sync back to the player doesn't work. The `D-Bus` and `MPRIS` interfaces are working, but anything requiring sending commands to the player doesn't work.
* Artwork is not downloaded.
* Playing from certain Apple devices to Shairport Sync does not work as expected. For example:
  * When using a service with the ability to play directly on HomePod (e.g. Apple Music). If the music starts on the HomePod, or is the first device selected from the AirPlay list on iOS, when Shairport Sync is then selected iOS shows it as not connected (even though it is). Therefore the expected volume controls are not displayed. A workaround for this is to force an AirPlay stream to the HomePod (rather than just acting as a remote controller), this can be done by selected Shairport Sync _before_ the HomePod. Note, this issue is not present for other services such as SoundCloud which do not integrate into the HomePod like Apple Music does.
  * The above example is true also when trying to select Shairport Sync from the AirPlay menu on the HomePod from the Home app.

More About What Works
---
* Two types of audio are received -- "Realtime" streams of CD quality ALAC (like AirPlay 1) and "Buffered Audio" streams of AAC stereo at 44,100 frames per second. The selection of stream type is made by the player.
* Audio is synchronised with other AirPlay 2 devices, including AirPlay 2 devices that have their own master clocks. (Limitation: This has not been tested with multiple nearly-identical master clock devices such as with two HomePod minis -- Shairport Sync may get confused about which is the current master.)
* Shairport Sync continues to support AirPlay 1, and offers an AirPlay 1 compatibility mode for situations where iTunes or macOS Music plays to multiple speakers and one of more of them is compatible with AirPlay 1 only.

What You Need
---
* A Raspberry Pi. See table below for tested devices, feel free to open an issue or pull request if you have Shairport Sync working on a device not listed here!
* [`NQPTP`](https://github.com/mikebrady/nqptp) is needed and must be run as `root`. It will use ports `319` and `320`, normally reserved for [PTP](https://standards.ieee.org/standard/1588-2008.html) clocks. If you are using PTP clock services for something else, you can't install [`NQPTP`](https://github.com/mikebrady/nqptp) and so you can't use Shairport Sync.(FYI, most computers do not have a PTP clock running -- instead, they may use a [Network Timing Protocol (NTP)](http://www.ntp.org) service to keep the system clock synchronised with world time. However, as noted above, macOS _does_ use a PTP clock to support AirPlay 2.)

| Device              | Native   | Docker         | Notes |
|---------------------|----------|----------------|-------|
| Raspberry Pi 4      | Working  | Working        |       |
| Raspberry Pi 3      | Working  | Working        |       |
| Raspberry Pi Zero W | Untested | Mostly working | Since the Pi Zero is low powered, issues can arise fairly quickly when trying to run additional programs, this currently includes quickly changing the volume repeatedly. Total CPU usage sits at around 75% when using Shairport Sync. Updates to Shairport Sync could make this better or worse. |
