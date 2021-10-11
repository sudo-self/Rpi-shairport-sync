Build Instructions for AirPlay 1
==
Here are simple instructions for building and installing Shairport Sync to provide an AirPlay 1 service on a recent Linux based on Debian, including Ubuntu and Raspberry Pi OS.

In the commands below, note the convention that a `#` prompt means you are in superuser mode and a `$` prompt means you are in a regular unprivileged user mode. You can use `sudo` *("SUperuser DO")* to temporarily promote yourself from user to superuser, if permitted. For example, if you want to execute `apt-get update` in superuser mode and you are in user mode, enter `sudo apt-get update`.

### Configure and Update
Do the usual update and upgrade:
```
# apt-get update
# apt-get upgrade
```
(Separately, on a Raspberry Pi only, if you haven't done so already, consider using the `raspi-config` tool to expand the file system to use the entire card.)

### Turn Off WiFi Power Management
If you are using WiFi, you should turn off WiFi Power Management:
```
# iwconfig wlan0 power off
```
WiFi Power Management will put the WiFi system in low-power mode when the WiFi system is considered inactive, and in this mode it may not respond to events initiated from the network, such as AirPlay requests. Hence, WiFi Power Management should be turned off. (See [TROUBLESHOOTING.md](https://github.com/mikebrady/shairport-sync/blob/master/TROUBLESHOOTING.md#wifi-adapter-running-in-power-saving--low-power-mode) for more details.)

Reboot the computer.

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

### Remove Old Startup and Service Scripts
You should also remove the startup script and service definition files `/etc/systemd/system/shairport-sync.service`, `/lib/systemd/system/shairport-sync.service`, `/etc/init.d/shairport-sync`, `/etc/dbus-1/system.d/shairport-sync-dbus.conf` and `/etc/dbus-1/system.d/shairport-sync-mpris.conf` if they exist – new ones will be installed if necessary.

### Reboot after Cleaning Up
If you removed any installations of Shairport Sync or any of its startup script files in the last two steps, you should reboot.

### Build and Install
Okay, now let's get the tools and sources for building and installing Shairport Sync.

First, install the packages needed by Shairport Sync:
```
# apt-get install build-essential git xmltoman autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev
```
Next, download Shairport Sync, check out the `development` branch, configure it, compile and install it:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ git checkout development
$ autoreconf -fi
$ ./configure --sysconfdir=/etc --with-alsa --with-soxr --with-avahi --with-ssl=openssl --with-systemd
$ make
$ sudo make install
```
By the way, the `autoreconf` step may take quite a while -- be patient!

### Configure

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

The next step is to enable Shairport Sync to start automatically on boot up:
```
# systemctl enable shairport-sync
```
Finally, either reboot the Pi or start the `shairport-sync` service:
```
# systemctl start shairport-sync
```
The Shairport Sync AirPlay service should now appear on the network with a service name made from the computer's hostname with the first letter capitalised, e.g. hostname `ubuntu` gives a service name `Ubuntu`. You can change the service name and set a password in the configuration file.

Connect and enjoy...
