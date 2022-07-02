# Fedora Installation Guide [Needs updating for Airplay 2]

Update everything...
```
# yum update
```
Install the toolchain and prerequisites, if necessary:
```
# yum install make automake gcc gcc-c++
# yum install alsa-lib-devel autoconf automake avahi-devel libconfig-devel \
    openssl-devel popt-devel soxr-devel
```
For AirPlay 2, you'll also need to install the following prerequisites. But first, ensure you have enabled the RPM Fusion software repositories to the "Free" level. See [here](https://docs.fedoraproject.org/en-US/quick-docs/setup_rpmfusion) for details and a guide.
```
# yum install ffmpeg ffmpeg-devel libplist-devel libsodium-devel libgcrypt-devel libuuid-devel vim-common
```

## Build
### NQPTP
**Note:** Skip this section if you are building Classic Shairport Sync.

Download, install, enable and start NQPTP from [here](https://github.com/mikebrady/nqptp).

### Shairport Sync

#### Please use `git` to clone the repository!
As you probably know, you can download the repository in two ways: (1) using `git` to clone it  -- recommended -- or (2) downloading the repository as a ZIP archive. Please use the `git` method. The reason it that when you use `git`, the build process can incorporate the `git` build information in the version string you get when you execute the command `$ shairport-sync -V`. This will be very useful for identifying the exact build if you are making comments or bug reports. Here is an example:
```
Version with git information:
4.0-dev-138-g2789572-AirPlay2-OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc

Version without git information:
4.0-dev-AirPlay2-OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc
```

#### Build and Install
Download Shairport Sync, check out the `development` branch and configure, compile and install it.

Omit the `--with-airplay-2` from the `./configure` options if you are building Classic Shairport Sync:

```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ git checkout development
$ autoreconf -fi
$ ./configure --sysconfdir=/etc --with-alsa \
    --with-soxr --with-avahi --with-ssl=openssl --with-systemd --with-airplay-2
$ make -j
# make install
```
By the way, the `autoreconf` step may take quite a while – please be patient!

## Configuration
By default when you start Shairport Sync, it will play to the default output device. You can configure Shairport Sync to use a different device -- for instance, if you have a special high-quality DAC, you may wish for Shairport Sync to use it instead.

Furthermore, by default, Shairport Sync will not use any hardware attenuator (aka "mixer") an output device may have. A mixer is used to implement volume control. Without a mixer, Shairport Sync will use a built-in software-based attenuator to control output level. The catch here is that if there is really a mixer and Shairport Sync isn't using it, the level the mixer happens to be set to represents the highest output Shairport Sync can attain. It may be set too low to be heard. You can use an external program such as `alsamixer` to look for and set the levels of mixers, but Shairport Sync can control a mixer directly if you configure it.

Here is an example, based on using the standard `alsa` backend, of configuring Shairport Sync's output device and mixer. 
A list of `alsa` output devices is given at the end of the help information. For example, at the end of the output from the command `$ shairport-sync -h`, the following might appear:

```
...
Available audio backends:
    alsa (default)

Settings and options for the audio backend "alsa":
    -d output-device    set the output device, default is "default".
    -c mixer-control    set the mixer control name, default is to use no mixer.
    -m mixer-device     set the mixer device, default is the output device.
    -i mixer-index      set the mixer index, default is 0.
    hardware output devices:
      "hw:AudioPCI"
```
Using a program such as `alsamixer`, the name of a mixer (i.e. an attenuator or volume control that could be used to control the output level) can be determined. In this case, the name of the mixer is `Master`.

Here are the important options for the Shairport Sync configuration file at `/etc/shairport-sync.conf`. Note that everything is case-sensitive:
```
// Sample Configuration File for Shairport Sync using the built-in soundcard's DAC

alsa =
{
  output_device = "hw:AudioPCI";
  mixer_control_name = "Headphone";
};

```

(There are lots more settings you can make in the configuration file – please take a look at `/etc/shairport-sync.conf.sample`.)

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
The user doesn't need to be privileged, but must be a member of the `audio` group for access to the `alsa` subsystem.


### Adding to Home

Follow the steps in [ADDINGTOHOME.md](https://github.com/mikebrady/shairport-sync/blob/development/ADDINGTOHOME.md) to add your Shairport Sync device to the Apple Home system.

## Update Your Mac!

Many AirPlay 2 bugs have been fixed in recent versions of macOS, so it is strongly recommended that you update your Mac.

## Using Shairport Sync

The Shairport Sync AirPlay service should appear on the network with a service name made from the machine's hostname with the first letter capitalised, e.g. hostname `ubuntu` gives a service name `Fedora`. You can change the service name in the configuration file.

Connect and enjoy...
