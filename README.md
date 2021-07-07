
Shairport Sync
=============
* Shairport Sync is an AirPlay audio player – it plays audio streamed from Apple devices and from AirPlay sources such as [OwnTone](https://github.com/owntone/owntone-server) (formerly `forked-daapd`).
* Shairport Sync can be built for "classic" AirPlay 1 or limited Airplay 2 support. The AirPlay 2 build is much bigger and requires more libraries and RAM at build time and run time.

AirPlay 2
---
For the latest on AirPlay 2, please visit [AIRPLAY2.md](https://github.com/aillwee/shairport-sync/blob/development/AIRPLAY2.md).

Guides
---
* A brief guide to building classic Shairport Sync for AirPlay 1 is available at [BUILDFORAP1.md](https://github.com/aillwee/shairport-sync/blob/development/BUILDFORAP1.md).
* A guide to building Shairport Sync for AirPlay 2 on Linux is available at [BUILDFORAP2.md](https://github.com/aillwee/shairport-sync/blob/development/BUILDFORAP2.md).
* A guide for building on FreeBSD is forthcoming.

Note
---
Shairport Sync does not support AirPlay video or photo streaming.

More Information
---
For more information, and for a more complete account of how to build Shairport Sync for AirPlay 1, please visit [MOREINFO.md](https://github.com/aillwee/shairport-sync/blob/development/MOREINFO.md).

Acknowledgements
---
For the development of AirPlay 2 support, special thanks are due to:
* [JD Smith](https://github.com/jdtsmith) for really thorough testing, support and encouragement.
* [ejurgensen](https://github.com/ejurgensen) for advice and [code to deal with pairing and encryption](https://github.com/ejurgensen/pair_ap).
* [ckdo](https://github.com/ckdo) for pointing the way, particularly with pairing and encryption protocols, with a [functional Python implementation](https://github.com/ckdo/airplay2-receiver) of AirPlay 2.
* [invano](https://github.com/invano) for showing what might be possible and for initial Python development.
* [Charles Omer](https://github.com/charlesomer) for Docker autoation (forthcoming), testing, encouragement, enthusiasm.

And of course, thanks to everyone who has supported and improved Shairport Sync over the years.
