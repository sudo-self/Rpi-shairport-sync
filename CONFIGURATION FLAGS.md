# Configuration Flags
When the application is being built, these flags determine what capabilities are included in Shairport Sync.
For example, to include DSP capabilities – needed for a loudness filter – you would include the `--with-convolution` flag in the list of
options at the `./configure ...` stage when building the application. 

In the following sections, Shairport Sync's compilation configuration options are detailed by catgegory.

## AirPlay 2 or AirPlay

| Flag |
| ---- |
| `--with-airplay-2` |

AirPlay 2 is the current version of the AirPlay protocol. It offers multi-room operation and integration with the Home application. However, the Shairport Sync implementation is doesn't support iTunes on Windows, and its integration with the Home app and support for remote control is incomplete. Additionally, it requires a somewhat more powerful computer system (Raspberry Pi 2 equivalent or better) and a recent (2018 or later) version of a Debian-like Linux, Alpine Linux or FreeBSD. It has not been tested on other Linux distributions such as OpenWrt. Finally, AirPlay 2 can be lossy – in one mode of operation, audio is encoded in 256kbps AAC.

AirPlay (aka AirPlay 1) is an older version of the AirPlay protocol. If offers multi-room operation to iTunes on macOS or Windows, the Music app on macOS and some third-party computer applications such as [OwnTone](https://owntone.github.io/owntone-server/). It will run on lower powered machines, e.g. the original Raspberry Pi and many embedded devices. This version of AirPlay is lossless – audio is received in 44,100 frames per second 16-bit interleaved stereo ALAC format. It is compatible with a wider range of Linux distributions, back to around 2012. However, support for this version of AirPlay seems to be gradually waning. It does not offer multi-room operation to iOS, iPadOS or AppleTV and is incompatible with HomePod. It is not integrated with the Home app.

To build Shairport Sync for AirPlay 2, include the `--with-airplay-2` option in the `./configure ...` options. You will also have to include extra libraries. Omitting this option will cause Shairport Sync to be built for the older AirPlay protocol.

## Audio Output
| Flags |
| ----- |
| `--with-alsa` |
| `--with-sndio` |
| `--with-pa` |
| `--with-pw` |
| `--with-ao` |
| `--with-jack` |
| `--with-soundio` |
| `--with-stdout` |
| `--with-pipe` |

Shairport Sync has a number of different "backends" that send audio to an onward destination, e.g. an audio subsystem for output to a Digital to Audio Converter and thence to loudspeakers, or to a pipe for further processing. You can use configuration options to include support for multiple backends at build time and select one of them at runtime.

Here are the audio backend configuration options:

- `--with-alsa` Output to the Advanced Linux Sound Architecture ([ALSA](https://www.alsa-project.org/wiki/Main_Page)) system. This is recommended for highest quality.
- `--with-sndio` Output to the FreeBSD-native [sndio](https://sndio.org) system.
- `--with-pa` Include the PulseAudio audio back end.
- `--with-pw` Output to the [PipeWire](https://pipewire.org) system.
- `--with-ao` Output to the [libao](https://xiph.org/ao/) system. No synchronisation.
- `--with-jack` Output to the [Jack Audio](https://jackaudio.org) system.
- `--with-soundio` Include an optional backend module for audio to be output through the [`soundio`](http://libsound.io) system. No synchronisation.
- `--with-stdout` Include an optional backend module to enable raw audio to be output through standard output (`STDOUT`).
- `--with-pipe` Include an optional backend module to enable raw audio to be output through a unix pipe.

### PulseAudio and PipeWire
Many recent Linux distributions with a GUI -- "desktop" Linuxes -- use PulseAudio or PipeWire to handle sound. There are two things to consider with these sound servers: 
1. They may not always be available: a sound server generally becomes available when a user logs in via the GUI and disappears when the user logs out; it is not available when the system starts up and it is not available to non-GUI users. This means that Shairport Sync can not run as a daemon (see "Daemonisation" below) using a sound server unless the sound server is configured as a system-wide service.
2. The fidelity of the audio is unknown: once audio is delivered to the sound server, it is unknown what happens to it as it is processed through PulseAudio to arrives eventually at the loudspeakers.

It should be noted that both PulseAudio and PipeWire provide a default ALSA pseudo device that enables ALSA-compatible programs to send audio. Shairport Sync can therefore use the ALSA backend with PulseAudio- or PipeWire-based systems. 

## Audio Options
| Flags |
| ----- |
| `--with-soxr` |
| `--with-apple-alac` |
| `--with-convolution` |

- `--with-soxr` Allows Shairport Sync to use [libsoxr](https://sourceforge.net/p/soxr/wiki/Home/)-based resampling for improved interpolation. Recommended. 
- `--with-apple-alac` Allows Shairport Sync to use the Apple ALAC Decoder. Requires [`libalac`](https://github.com/mikebrady/alac).
- `--with-convolution` Includes a convolution filter that can be used to apply effects such as frequency and phase correction, and a loudness filter that compensates for the non-linearity of the human auditory system. Requires `libsndfile`.

## Metadata
| Flags |
| ----- |
| `--with-metadata` |

Metadata such as track name, artist name, album name, cover art and more can be requested from the player and passed to other applications.

- `--with-metadata` Adds support for Shairport Sync to request metadata and to pipe it to a compatible application. See https://github.com/mikebrady/shairport-sync-metadata-reader for a sample metadata reader.

## Inter Process Communication

| Flags |
| ----- |
| `--with-mqtt-client` |
| `--with-dbus-interface` |
| `--with-dbus-test-client` |
| `--with-mpris-interface` |
| `--with-mpris-test-client` |

Shairport Sync offers three Inter Process Communication (IPC) interfaces: an [MQTT](https://mqtt.org) interface, an [MPRIS](https://specifications.freedesktop.org/mpris-spec/latest/)-like interface and a Shairport Sync specific "native" D-Bus interface loosely based on MPRIS. 
The options are as follows:

- `--with-mqtt-client` Includes a client for [MQTT](https://mqtt.org),
- `--with-dbus-interface`  Includes support for the native Shairport Sync D-Bus interface,
  - `--with-dbus-test-client` Compiles a D-Bus test client application,
- `--with-mpris-interface` Includes support for a D-Bus interface conforming as far as possible with the MPRIS standard,
  - `--with-mpris-test-client` Compiles an MPRIS test client application.

D-Bus and MPRIS commands and properties can be viewed using utilities such as [D-Feet](https://wiki.gnome.org/Apps/DFeet).

##### MPRIS
The MPRIS interface has to depart somewhat from full MPRIS compatibility due to logical differences between Shairport Sync and a full standalone audio player such as Rhythmbox. Basically there are some things that Shairport Sync itself can not control that a standalone player can control.
A good example is volume control. MPRIS has a read-write `Volume` property, so the volume can be read or set. However, all Shairport Sync can do is *request* the player to change the volume control. This request may or may not be carried out, and it may or may not be done accurately. So, in Shairport Sync's MPRIS interface, `Volume` is a read-only property, and an extra command called `SetVolume` is provided.

## Other Configuration Options

### Configuration Files
| Flags |
| ----- |
| `--sysconfdir=<directory>` |

Shairport Sync will look for a configuration file – `shairport-sync.conf` by default – when it starts up. By default, it will look in the directory specified by the `sysconfdir` configuration variable, which defaults to `/usr/local/etc`. This is normal in BSD Unixes, but is unusual in Linux. Hence, for Linux installations, you need to set the `sysconfdir` variable to `/etc` using the configuration setting `--sysconfdir=/etc`.

### Daemonisation
| Flags |
| ----- |
| `--with-libdaemon` |
| `--with-piddir=<pathname>` |

A [daemon](https://en.wikipedia.org/wiki/Daemon_(computing)) is a computer program that runs as a background process, rather than being under the direct control of an interactive user. Shairport Sync is designed to run as a daemon.
FreeBSD and most recent Linux distributions can run an application as a daemon without special modifications. However, in certain older distributions and in special cases it may be necessary to enable Shairport Sync to daemonise itself. Use the `--with-libdaemon` configuration option:

- `--with-libdaemon` Includes a demonising library needed if you want Shairport Sync to demonise itself with the `-d` option. Not needed for `systemd`-based systems which demonise programs differently.
- `--with-piddir=<pathname>` Specifies a pathname to a directory in which to write the PID file which is created when Shairport Sync daemonises itself and used to locate the deamon process to be killed with the `-k` command line option.

### Automatic Start
| Flags |
| ----- |
| `--with-systemd` |
| `--with-systemdsystemunitdir=<dir>` |
| `--with-systemv` |
| `--with-freebsd-service` |
| `--with-sygwin-service` |

Daemon programs such as Shairport Sync need to be started automatically, so that the service they provide becomes available without further intervention. Typically this is done using startup scripts. Four options are provided – two for Linux, one for FreeBSD and one for CYGWIN. In Linux, the choice depends on whether [systemd](https://en.wikipedia.org/wiki/Systemd) is used or not. If `systemd` is installed, then the `--with-systemd` option is suggested. If not, the `--with-systemv` option is suggested.

- `--with-systemd` Includes a script to create a Shairport Sync service that can optionally launch automatically at startup on `systemd`-based Linuxes. Default is not to to install. Note: an associated special-purpose option allows you to specify where the `systemd` service file will be placed:
  - `--with-systemdsystemunitdir=<dir>` Specifies the directory for `systemd` service files.
- `--with-systemv` Includes a script to create a Shairport Sync service that can optionally launch automatically at startup on System V based Linuxes. Default is not to to install.
- `--with-freebsd-service` Includes a script to create a Shairport Sync service that can optionally launch automatically at startup on FreeBSD. Default is not to to install.
- `--with-cygwin-service` Includes a script to create a Shairport Sync service that can optionally launch automatically at startup on CYGWIN. Default is not to to install.

### Cryptography
| Flags |
| ----- |
| `--with-ssl=openssl` |
| `--with-ssl=mbedtls` |
| `--with-ssl=polarssl` |

AirPlay 2 operation requires the OpenSSL libraries, so the option `--with-ssl=openssl` is mandatory.

For classic Shairport Sync, the options are as follows:

- `--with-ssl=openssl` Uses the [OpenSSL](https://www.openssl.org) cryptography toolkit. This is mandatory for AirPlay 2 operation.
- `--with-ssl=mbedtls` Uses the [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) cryptography library. Only suitable for classic AirPlay operation – do not include it in an AirPlay 2 configuration.
- `--with-ssl=polarssl` Uses the PolarSSL cryptography library. This is deprecated – PolarSSL has been replaced by Mbed TLS. Only suitable for classic AirPlay operation – do not include it in an AirPlay 2 configuration.

### Zeroconf/Bonjour
| Flags |
| ----- |
| `--with-avahi` |
| `--with-tinysvcmdns` |
| `--with-external-mdns` |
| `--with-dns_sd` |

AirPlay devices advertise their existence and status using [Zeroconf](http://www.zeroconf.org) (aka [Bonjour](https://en.wikipedia.org/wiki/Bonjour_(software))).

AirPlay 2 operation requires the [Avahi](https://www.avahi.org) libraries, so the option `--with-avahi` is mandatory.

For classic Shairport Sync, the options are as follows:

The Zeroconf-related options are as follows:
- `--with-avahi` Chooses [Avahi](https://www.avahi.org)-based Zeroconf support. This is mandatory for AirPlay 2 operation.
- `--with-tinysvcmdns` Chooses [tinysvcmdns](https://github.com/philippe44/TinySVCmDNS)-based Zeroconf support (deprecated).
- `--with-external-mdns` Supports the use of ['avahi-publish-service'](https://linux.die.net/man/1/avahi-publish-service) or 'mDNSPublish' to advertise the service on Bonjour/ZeroConf.
- `--with-dns_sd` Chooses `dns-sd` Zeroconf support.

### Miscellaneous
| Flag                 | Significance |
| -------------------- | ---- |
| `--with-os=<OSType>`   | Specifies the Operating System to target: One of `linux` (default), `freebsd`, `openbsd` or `darwin`. |
| `--with-configfiles` | Installs configuration files (including a sample configuration file) during `make install`. |
| `--with-pkg-config`  | Specifies the use of `pkg-config` to find libraries. (Obselete for AirPlay 2. Special purpose use only.) |

