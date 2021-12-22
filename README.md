
Shairport Sync
=============
* Shairport Sync is an AirPlay audio player – it plays audio streamed from Apple devices and from AirPlay sources such as [OwnTone](https://github.com/owntone/owntone-server) (formerly `forked-daapd`).
* Shairport Sync can provide "classic" AirPlay 1 or limited Airplay 2 support. The AirPlay 2 build is much more demanding. It requires a faster system, more storage and more RAM both at build time and run time.
* Shairport Sync does not support AirPlay video or photo streaming.

AirPlay 2
---
For the latest on AirPlay 2, please visit [AIRPLAY2.md](https://github.com/mikebrady/shairport-sync/blob/development/AIRPLAY2.md).

Guides
---
* A brief guide to building classic Shairport Sync (for AirPlay 1) is available at [BUILDFORAP1.md](https://github.com/mikebrady/shairport-sync/blob/development/BUILDFORAP1.md).
* A guide to building Shairport Sync for AirPlay 2 on Linux is available at [BUILDFORAP2.md](https://github.com/mikebrady/shairport-sync/blob/development/BUILDFORAP2.md).
* A guide to building for AirPlay 2 on FreeBSD is forthcoming.
* Please note, a Docker image including Airplay 2 support is available under the docker folder of this repo.

More Information
---
For more information, please visit [MOREINFO.md](https://github.com/mikebrady/shairport-sync/blob/development/MOREINFO.md).

Acknowledgements
---
For the development of AirPlay 2 support, special thanks are due to:
* [JD Smith](https://github.com/jdtsmith) for really thorough testing, support and encouragement.
* [ejurgensen](https://github.com/ejurgensen) for advice and [code to deal with pairing and encryption](https://github.com/ejurgensen/pair_ap).
* [ckdo](https://github.com/ckdo) for pointing the way, particularly with pairing and encryption protocols, with a [functional Python implementation](https://github.com/ckdo/airplay2-receiver) of AirPlay 2.
* [invano](https://github.com/invano) for showing what might be possible and for initial Python development.
* [Charles Omer](https://github.com/charlesomer) for Docker automation, repository management automation, testing, encouragement, enthusiasm.

And of course, thanks to everyone who has supported and improved Shairport Sync over the years.