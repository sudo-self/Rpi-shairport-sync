
Shairport Sync
=============
* Shairport Sync is an AirPlay audio player – it plays audio streamed from Apple devices and from AirPlay sources such [ForkedDaapd](http://ejurgensen.github.io/forked-daapd/).
* Shairport Sync can be built to support AirPlay 1 only or AirPlay 2. The AirPlay 2 build requires a good deal of extra library support and may not fit into smaller devices.
* **AirPlay 2 support is experimental and incomplete.** The focus of the development effort is on a good, stable, audio experience. Thus, many out-of-scope features are missing or broken. So integration with Apple's Home app is missing; remote control doesn't work.
* When built for AirPlay 1, Shairport Sync runs on Linux, FreeBSD and OpenBSD.
* AirPlay 2 support is only available on recent Linux builds.

Guides
===
* A brief guide to building Shairport Sync for AirPlay 1 is available at [BUILDFORAP1.md](https://github.com/aillwee/shairport-sync/blob/development/BUILDFORAP1.md).
* To build Shairport Sync for AirPlay 2, please follow the guide at [BUILDFORAP2.md](https://github.com/aillwee/shairport-sync/blob/development/BUILDFORAP2.md).

Note
===
Shairport Sync does not support AirPlay video or photo streaming.

More Information
===
For more information, and for a more complete account of how to build Shairport Sync for AirPlay 1, please visit [MOREINFO.md](https://github.com/aillwee/shairport-sync/blob/development/MOREINFO.md).

Acknowledgements
---
For the development of AirPlay 2 support, special thanks are due to:
* [JD Smith](https://github.com/jdtsmith) for really thorough testing, support and encouragement.
* [ejurgensen](https://github.com/ejurgensen) for advice and [code to deal with pairing and encryption](https://github.com/ejurgensen/pair_ap).
* [ckdo](https://github.com/ckdo) for pointing the way, particularly with pairing and encryption protocols, with a [functional Python implementation](https://github.com/ckdo/airplay2-receiver) of AirPlay 2.
* [invano](https://github.com/invano) for showing what might be possible and for initial Python development.
* [Charles Somer](https://github.com/charlesomer) for testing, encouragement and enthusiasm.

And of course, thanks to everyone who has supported and improved Shairport Sync over the years.
