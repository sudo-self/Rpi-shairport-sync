# Fedora Installation Guide
This guide is for a recent version of Fedora Linux (Fedora 36) outputting to the PipeWire sound system through a built-in ALSA adapter.

Shairport Sync can be built as an AirPlay 2 player (with [some limitations](https://github.com/mikebrady/shairport-sync/blob/development/AIRPLAY2.md#features-and-limitations)) or as "classic" Shairport Sync – a player for the older, but still supported, AirPlay (aka "AirPlay 1") protocol.

Fedora uses PipeWire for audio management [since Fedora 34](https://fedoramagazine.org/pipewire-the-new-audio-and-video-daemon-in-fedora-linux-34/). An [adapter](https://wiki.archlinux.org/title/PipeWire#ALSA_clients) is included to route audio from ALSA-compatible sources into the PipeWire infrastructure.

It is recommended that Shairport Sync is built with the standard ALSA backend (`--with-alsa`).

Shairport Sync also offers a PipeWire (`--with-pw`) backend – still new and still under development. The PulseAudio (`--with-pa`) backend may also be used thanks to a PulseAudio-to-PipeWire adapter.

**Note:** The use of PipeWire, even indirectly, means that Shairport Sync can not be installed as a system service (aka a system daemon). The user must log in through the Fedora GUI for the audio received by Shairport Sync to be routed to the system audio output, e.g. the onboard speakers.

Overall, you'll be building and installing two programs. The first one is NQPTP, a companion app that Shairport Sync uses for AirPlay 2 timing, and the second one is Shairport Sync itself. Build and install NQPTP first. If you are building classic Shairport Sync, NQPTP is unnecessary and can be omitted.

In the commands below, note the convention that a `#` prompt means you are in superuser mode and a `$` prompt means you are in a regular unprivileged user mode. You can use `sudo` *("SUperuser DO")* to temporarily promote yourself from user to superuser, if permitted. For example, if you want to execute `yum update` in superuser mode and you are in user mode, enter `sudo yum update`.

## Turn Off WiFi Power Management
If you are using WiFi, you should turn off WiFi Power Management, e.g. if the WiFi device name is `wlp5s0b1`:

```
# iw dev wlp5s0b1 set power_save off
```
The reason for this is that WiFi Power Management will put the WiFi system in low-power mode when the WiFi system is considered inactive. In this mode, the system may not respond to events initiated from the network, such as AirPlay requests. Hence, WiFi Power Management should be turned off. (See [TROUBLESHOOTING.md](https://github.com/mikebrady/shairport-sync/blob/master/TROUBLESHOOTING.md#wifi-adapter-running-in-power-saving--low-power-mode) for more details.)

## Enable RPM Fusion Software Repositories (AirPlay 2 Only)
For AirPlay 2, it is important to [enable](https://docs.fedoraproject.org/en-US/quick-docs/setup_rpmfusion) the RPM Fusion software repositories, at least to the "Free" level. This is so that [ffmpeg](https://ffmpeg.org) libraries will be installed that include a suitable AAC decoder.

## Update Everything
```
# yum update
```
## Remove Old Stuff
### Remove Old Copies of Shairport Sync
Before you begin building Shairport Sync, it's best to search for and remove any existing copies of the application, called `shairport-sync`. Use the command `$ which shairport-sync` to find them. For example, if `shairport-sync` has been installed previously, this might happen:
```
$ which shairport-sync
/usr/local/bin/shairport-sync
```
Remove it as follows:
```
# rm /usr/local/bin/shairport-sync
```
Do this until no more copies of `shairport-sync` are found.

### Remove Old Service Files
You should also remove any of the following service files that may be present: `/etc/systemd/system/shairport-sync.service`, `/lib/systemd/system/shairport-sync.service`, `/etc/dbus-1/system.d/shairport-sync-dbus.conf`, `/etc/dbus-1/system.d/shairport-sync-mpris.conf`, and `/etc/init.d/shairport-sync`. New ones will be installed if necessary.

### Reboot after Cleaning Up
If you removed any installations of Shairport Sync or any of its startup script files in the last two steps, you should reboot.


## Install Toolchain and Libraries
Okay, now let's get the tools and libraries for building and installing Shairport Sync (and NQPTP).

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

Download, install, enable and start NQPTP from [here](https://github.com/mikebrady/nqptp). By the way, Fedora has a firewall running by default, so make sure you enable UDP traffic to and from ports 319 and 320, as noted in the NQPTP guide.

### Shairport Sync

#### Please use `git` to clone the repository!
As you probably know, you can download the repository in two ways: (1) using `git` to clone it  – recommended – or (2) downloading the repository as a ZIP archive. Please use the `git` method. The reason it that when you use `git`, the build process can incorporate the `git` build information in the version string you get when you execute the command `$ shairport-sync -V`. This will be very useful for identifying the exact build if you are making comments or bug reports. Here is an example:
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
By default when you start Shairport Sync, it will play to the default output device, which is just what is needed here – the default ALSA device routes audio into Fedora's PipeWire infrastructure. You can configure many Shairport Sync settings in the configuration file, installed during the `# make install` step at `/etc/shairport-sync.conf` along with a sample at `/etc/shairport-sync.conf.sample`.

### Automatic Start

To run automatically after user login, Shairport Sync can be installed as a user service.

Placeholder: please refer to the discussion in [Issue #1260](https://github.com/mikebrady/shairport-sync/issues/1260).

### Running From the Command Line

You may wish to run Shairport Sync from the command line. To enable debug messages and statistics, use the following:

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
