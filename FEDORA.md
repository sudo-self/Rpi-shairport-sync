# Fedora Installation Guide

Fedora uses PipeWire for audio management [since Fedora 34](https://fedoramagazine.org/pipewire-the-new-audio-and-video-daemon-in-fedora-linux-34/). An [adapter](https://wiki.archlinux.org/title/PipeWire#ALSA_clients) for ALSA-compatible software is included which creates a virtual ALSA output device to route audio from ALSA-compatible sources into the PipeWire infrastructure. It is recommended that Shairport Sync is built with the standard ALSA backend (`--with-alsa`).

Shairport Sync also offers PipeWire (`--with-pw`) and PulseAudio (`--with-pa`) backends that may be used. The PipeWire backend is new and still undergoing development.

## Enable RPM Fusion Software Repositories (AirPlay 2 Only)
For AirPlay 2, it is important to [enable](https://docs.fedoraproject.org/en-US/quick-docs/setup_rpmfusion) the RPM Fusion software repositories, at least to the "Free" level. This is so that [ffmpeg](https://ffmpeg.org) libraries will be installed that include a suitable AAC decoder.

## Update Everything
```
# yum update
```
## Install Toolchain and Libraries

```
# yum install make automake gcc gcc-c++
# yum install autoconf automake avahi-devel libconfig-devel openssl-devel popt-devel soxr-devel
# yum install alsa-lib-devel # for the ALSA back end -- used in this build
```
(To build the PipeWire backend, install the `pipewire-devel` library; for the PulseAudio backend, you'll need the `pulseaudio-libs-devel` library.)

For AirPlay 2 operation, extra libraries must be installed. Before taking this step, once again please ensure the you have [enabled](https://docs.fedoraproject.org/en-US/quick-docs/setup_rpmfusion) RPM Fusion software repositories at least to the "Free" level. If this is not done, ffmpeg libraries will be installed that lack a suitable AAC decoder, preventing Shairport Sync from working.

Install the extra libraries with the following command:
```
# yum install ffmpeg ffmpeg-devel libplist-devel libsodium-devel libgcrypt-devel libuuid-devel vim-common
```

## Build
### NQPTP
Skip this section if you are building Classic Shairport Sync – NQPTP is not used by Classic Shairport Sync.

Download, install, enable and start NQPTP from [here](https://github.com/mikebrady/nqptp). By the way, Fedora has a firewall running by default, so make sure you eneable UDP traffic to and from ports 319 and 320, as noted in the NQPTP guide.

### Shairport Sync

#### Please use `git` to clone the repository!
As you probably know, you can download the repository in two ways: (1) using `git` to clone it  -- recommended -- or (2) downloading the repository as a ZIP archive. Please use the `git` method. The reason it that when you use `git`, the build process can incorporate the `git` build information in the version string you get when you execute the command `$ shairport-sync -V`. This will be very useful for identifying the exact build if you are making comments or bug reports. Here is an example:
```
Version with git information:
4.1-dev-389-gf317161a-AirPlay2-OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc

Version without git information:
4.1-dev-AirPlay2-OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc
```

#### Build and Install
Download Shairport Sync, check out the `development` branch and configure, compile and install it.

* Omit `--with-airplay-2` from the `./configure` options to build Classic Shairport Sync.

```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ git checkout development
$ autoreconf -fi
$ ./configure --sysconfdir=/etc --with-alsa \
    --with-soxr --with-avahi --with-ssl=openssl --with-systemd --with-airplay-2
```
If you get errors during the `./configure` step, check again that you have installed the toolchain and libraries correctly.
```
$ make -j
# make install
```
By the way, the `autoreconf` step may take quite a while – please be patient!

## Configuration
By default when you start Shairport Sync, it will play to the default output device, which is just what is needed here -- the default ALSA device routes audio into Fedora's PipeWire infrastructure. You can configure many Shairport Sync settings in the configuration file, installed during the `# make install` step at `/etc/shairport-sync.conf` along with a sample at `/etc/shairport-sync.conf.sample`.

### Automatic Start

To enable Shairport Sync to start automatically on boot up:
```
# systemctl enable shairport-sync
```
Now, either reboot or start the `shairport-sync` service:
```
# systemctl start shairport-sync
```

### Running From the Command Line

You may wish to run Shairport Sync from the command line (but remember to ensure it is not already running as a daemon). To enable debug messages and statistics, use the following:

```
$ shairport-sync -v --statistics
```
The user doesn't need to be privileged, but must be a member of the `audio` group to access the `alsa` subsystem.

### Adding to Home

If you have built it for AirPlay 2, you can follow the steps in [ADDINGTOHOME.md](https://github.com/mikebrady/shairport-sync/blob/development/ADDINGTOHOME.md) to add your Shairport Sync device to the Apple Home system.

## Update Your Mac!

Many AirPlay 2 bugs have been fixed in recent versions of macOS, so it is strongly recommended that you update your Mac.

## Using Shairport Sync

The Shairport Sync AirPlay service should appear on the network with a service name made from the machine's hostname with the first letter capitalised, e.g. hostname `fedora` gives a service name `Fedora`. You can change the service name in the configuration file.

Connect and enjoy...
