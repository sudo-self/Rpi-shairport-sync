Experimental Build Instructions for AirPlay 2
==
* This code is experimental and much of the normal functionality of Shairport Sync is broken. Please do not use it in situations where reliable service is expected!
* Be especially careful with audio systems capable of very high volume output -- the volume control in this software may not be reliable!
* Initially, at least, leave the settings in the configuration file at default except as noted below.
* At the time of writing, May 2, 2021, everything is on the latest version of the software -- macOS 11..3, iOS 14.5, Raspberry Pi OS 5.10.17-v7l+ (Buster), Ubuntu 20.04 -- fully updated.
* AirPlay 2 seems to use a timing system based on the [IEEE-1588 Precision Timing Protocol (PTP)](https://standards.ieee.org/standard/1588-2008.html). Shairport Sync relies on a program called [`nqptp`](https://github.com/mikebrady/nqptp) to monitor PTP signals. This program uses ports 319 and 320 and replaces any PTP service you have on the computer.
  In addition, `nqptp` must run with `root` privileges.  
 (FYI, most computers do not have a PTP clock running -- instead, they may use a [Network Timing Protocol (NTP)](http://www.ntp.org) service to keep the system clock synchronised with world time.)
* When Shairport Sync is updated, you should check and update `nqptp` *before* building the update.
* Build instructions are likely to change.

Build and Install Instructions
==
Overall, you'll be installing two programs. The first one is `nqptp` and the second one is Shairport Sync itself.

In the commands below, for a Debian-like Linux, note the convention that a `#` prompt means you are in superuser mode and a `$` prompt means you are in a regular unprivileged user mode. You can use `sudo` *("SUperuser DO")* to temporarily promote yourself from user to superuser, if permitted. For example, if you want to execute `apt-get update` in superuser mode and you are in user mode, enter `sudo apt-get update`.

### Turn Off WiFi Power Management
If you are using WiFi, you should turn off WiFi Power Management:
```
# iwconfig wlan0 power off
```
WiFi Power Management will put the WiFi system in low-power mode when the WiFi system is considered inactive. In this mode, the system may not respond to events initiated from the network, such as AirPlay requests. Hence, WiFi Power Management should be turned off. (See [TROUBLESHOOTING.md](https://github.com/mikebrady/shairport-sync/blob/master/TROUBLESHOOTING.md#wifi-adapter-running-in-power-saving--low-power-mode) for more details.)

### Configure and Update
Do the usual update and upgrade:
```
# apt-get update
# apt-get upgrade
```
Reboot.

### Remove Old Copies
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

### Remove Old Startup Scripts
You should also remove the startup script files `/etc/systemd/system/shairport-sync.service` and `/etc/init.d/shairport-sync` if they exist – new ones will be installed in necessary.

### Reboot after Cleaning Up
If you removed any installations of Shairport Sync or any of its startup script files in the last two steps, you should reboot.

### Tools and Libraries
Okay, now let's get the tools and libraries for building and installing Shairport Sync.

First, install the packages needed by Shairport Sync:
```
# apt-get install build-essential git xmltoman autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev libglib2.0-dev \
    libplist-dev libsodium-dev libavutil-dev libavcodec-dev libavformat-dev uuid-dev libgcrypt-dev
```
(Note: if you don't want the `D-Bus` interface, you may omit `libglib2.0-dev`.)

### nqptp ###
Download, install and start/enable `nqptp` from [here](https://github.com/mikebrady/nqptp).

***Note:*** At present, Shairport Sync expects the `nqptp` repository folder to be in the same directory as the `shairport-sync` repository folder, as it will look for a header file at `../nqptp/nqptp-shm-structures.h`.

The `nqptp` service monitors PTP clocks. It provides a POSIX Shared Memory Interface (SMI)  at `/nqptp`. A shared
pthread mutex is contained within the interface, and to use it you need write access.

Next, download Shairport Sync, check out the `development` branch, configure it, compile and install it:
```
$ git clone git@github.com:aillwee/shairport-sync.git
$ cd shairport-sync
$ git checkout development
$ autoreconf -fi
$ ./configure --sysconfdir=/etc --with-alsa \
    --with-soxr --with-avahi --with-ssl=openssl --with-systemd --with-dbus-interface --with-airplay-2
$ make -j
# make install
```
By the way, the `autoreconf` step may take quite a while -- be patient!

Now to configure Shairport Sync. In this walkthrough, it will be configured for an `alsa` output device. A list of `alsa` output devices is given at the end of the help information. For example, on a Raspberry Pi, at the end of the output from the command `$ shairport-sync -h`, the following appears:

```
...
Settings and options for the audio backend "alsa":
    -d output-device    set the output device, default is "default".
    -c mixer-control    set the mixer control name, default is to use no mixer.
    -m mixer-device     set the mixer device, default is the output device.
    -i mixer-index      set the mixer index, default is 0.
    hardware output devices:
      "hw:Headphones"
```
Using a program such as `alsamixer`, the name of a mixer (i.e. an attenuator or volume control that could be used to control the output level) can be determined. In this case, the name of the mixer is `Headphone`.

Following this, here are the important options for the Shairport Sync configuration file at `/etc/shairport-sync.conf`. Note that everything is case-sensitive:
```
// Sample Configuration File for Shairport Sync on a Raspberry Pi using the built-in audio DAC
general =
{
  volume_range_db = 60;
};

alsa =
{
  output_device = "hw:Headphones";
  mixer_control_name = "Headphone";
};

```
The `volume_range_db = 60;` setting makes Shairport Sync use only the usable part of the Raspberry Pi's built-in audio card mixer's attenuation range. It may not be necessary for other output devices.

### Automatic Start ###

To enable Shairport Sync to start automatically on boot up:
```
# systemctl enable shairport-sync
```
Now, either reboot or start the `shairport-sync` service:
```
# systemctl start shairport-sync
```

### Running From the Command Line ###

You may wish to run Shairport Sync from the command line (but remember to ensure it is not already running as a daemon). To enable debug messages and statistics, use the following:

```
$ shairport-sync -vu --statistics
```
The user doesn't need to be privileged, but must be a member of the `audio` group for access to the `alsa` subsystem.

### Using Shairport Sync ###

The Shairport Sync AirPlay service should appear on the network with a service name made from the machine's hostname with the first letter capitalised, e.g. hostname `ubuntu` gives a service name `Ubuntu`. You can change the service name in the configuration file.

Connect and enjoy...

### Restart Your Mac! ###

At the time of writing, there seems to be a bug in the Mac Music app. It appears that, when the Mac has been awoken after sleeping, the Music app may not fully reawaken, and it will be unable to drive AirPlay 2 devices for more than a few seconds. The only know solution is to reboot.
