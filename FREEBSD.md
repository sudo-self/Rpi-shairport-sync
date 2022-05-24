Shairport Sync on FreeBSD using `sndio` [Partly updated for Airplay 2]
----
Shairport Sync runs natively on FreeBSD using the `sndio` back end, thanks to the work of [Tobias Kortkamp (t6)](https://github.com/t6).

[`sndio`](http://www.sndio.org) is *"a small audio and MIDI framework part of the OpenBSD project and ported to FreeBSD, Linux and NetBSD"* developed by Alexandre Ratchov (see also [this paper](http://www.openbsd.org/papers/asiabsdcon2010_sndio.pdf) for more details).

The build instructions here install back ends for `sndio` and/or `ALSA`. `ALSA` is, or course, the Advanced Linux Sound Architecture, so it is not "native" to FreeBSD, but has been ported to some architectures under FreeBSD. 

General
----
This build was done on a default build of `freebsd 12.3-RELEASE-p5`.

First, update everything:
```
# freebsd-update fetch
# freebsd-update install
```
Next, install the `pkg` package manager and update its lists:

```
# pkg
# pkg update
```

Subsystems
----
Install the Avahi subsystem. FYI, `avahi-app` is chosen because it doesn’t require X11. `nss_mdns` is included to allow FreeBSD to resolve mDNS-originated addresses – it's not actually needed by Shairport Sync. Thanks to [reidransom](https://gist.github.com/reidransom/6033227) for this.

```
# pkg install avahi-app nss_mdns
```
Add these lines to `/etc/rc.conf`:
```
dbus_enable="YES"
avahi_daemon_enable="YES"
```
Next, change the `hosts:` line in `/etc/nsswitch.conf` to
```
hosts: files dns mdns
```
Reboot for these changes to take effect.

Building
----

Install the packages that are needed for Shairport Sync to be downloaded and built:
```
# pkg install git autotools pkgconf popt libconfig openssl sndio
```
Add `alsa-utils` if you're wish to use ALSA. Omit `sndio` if you don't intend to use the `sndio` subsystem.

Add `libplist libsodium ffmpeg e2fsprogs-libuuid vim` if you are building for AirPlay 2.

At this point, if you intend to build Shairport Sync for AirPlay 2, you should build and install `NQPTP`, a companion application that provides AirPlay 2 timing support for Shairport Sync. You'll find the software itself and an installation guide [here](https://github.com/mikebrady/nqptp/blob/main/README.md). Once you have built, installed and enabled NQPTP, you can proceed with building Shairport Sync.

Now, download Shairport Sync from GitHub:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
```
If you are building the AirPlay 2 version of Shairport Sync, for the present you should checkout the `development` version:

```
$ git checkout development
```

Next, configure the build and compile the `shairport-sync` application:

```
$ autoreconf -i -f
$ ./configure  --with-avahi --with-ssl=openssl --with-sndio --with-os=freebsd --with-freebsd-service
$ make
```
Add `--with-alsa` if you wish to include the ALSA back end. Omit the `--with-sndio` if you don't want the `sndio` back end.

Omit the `--with-freebsd-service` if you don't want to install a FreeBSD startup script, runtime folder and user and group -- see below for more details.

Add `--with-airplay-2` if you wish to build the AirPlay 2 version of Shairport Sync.

Installation
----

Enter the superuser mode and do a `make install`:

```
$ su
# make install
```

With the `./configure` options shown above, this will install the `shairport-sync` program along with a sample configuration file at `/usr/local/etc/shairport-sync.conf`. A service startup script will also be installed to launch Shairport Sync as a daemon. In addition, a `shairport-sync` user and group will be added to enable `shairport-sync` to run with a low level of privilege (a primitive form of extra security).

Finally, edit `/usr/local/etc/shairport-sync.conf` to customise your installation, e.g. service name, etc. To make the `shairport-sync` daemon load at startup, add the following line to `/etc/rc.conf`:

```
shairport_sync_enable="YES"
```
You can launch the service as superuser, or simply reboot the machine.

Using the `sndio` backend
----

The `sndio` back end does not yet have a hardware volume control facility. You should set the volume to maximum before use, using, for example, the `mixer` command described below.

Setting Overall  Volume
----
The `mixer` command can be used for setting the output device's volume settings. You may have to experiment to figure out which settings are appropriate.

```
$ mixer vol 100 # sets overall volume
```
If you've installed `alsa-utils`, then `alsamixer` and friends will also be available.
