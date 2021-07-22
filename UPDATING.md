
### Updating Shairport Sync [Needs updating for Airplay 2]
This guide is for building and updating an installation of Shairport Sync.

If you updated Shairport Sync from a package, most of these instructions don't apply – please go to the section at the end called "Post Update Tasks". If you wish to build an installation of Shairport Sync to replace an installation that came from a package, instructions don't apply either – instead, please follow the guide at [INSTALL.md](https://github.com/mikebrady/shairport-sync/blob/master/INSTALL.md).

To do an update, you basically have to go through the whole process of building Shairport Sync again,
but a few steps are shorter because you've done them before; you won't have to reinstall the build tools or libraries needed, and you won't have to define the user and group or reconfigure the settings in the configuration file.

But before you begin, you should update and upgrade any packages.

Note that in this guide, a `$` prefix means you should be in the normal user mode; a `#` prefix means you should be in the superuser or root mode.

Here is the sequence for Raspberry Pi OS (Buster), which is based on Debian Buster. The same commands work for Ubuntu, and maybe more. Here, a non-`root` user with `sudo` privileges is assumed.

```
$ sudo apt-get update
$ sudo apt-get upgrade
```
Next, stop playing music to your device.

### Remove Old Copies
It's a good idea to search for and remove any existing copies of the application, called `shairport-sync`. Use the command `$ which shairport-sync` to find them. For example, if `shairport-sync` has been installed previously, this might happen:
```
$ which shairport-sync
/usr/local/bin/shairport-sync
```
Remove it as follows:
```
# rm /usr/local/bin/shairport-sync
```
Do this until no more copies of `shairport-sync` are found. This is especially important if you are building Shairport Sync over an old version that was installed from packages, as the built application will be installed in a different directory to the packaged installation.

### Remove Old Startup and Service Scripts
You should also remove the startup script and service definition files `/etc/systemd/system/shairport-sync.service`, `/lib/systemd/system/shairport-sync.service`, `/etc/init.d/shairport-sync`, `/etc/dbus-1/system.d/shairport-sync-dbus.conf` and `/etc/dbus-1/system.d/shairport-sync-mpris.conf` if they exist – new ones will be installed if necessary. As with the previous section, this is especially important if you are building Shairport Sync over an old version that was installed from packages, as the scripts for the built application may be different to those of the packaged version. If they are left in place, bad things can happen.

### Reboot after Cleaning Up
If you removed any installations of Shairport Sync or any of its startup script files in the last two steps, you should reboot.

### Building the Updated Shairport Sync
Now, to build, update and install Shairport Sync, if you still have the directory in which you previously built Shairport Sync, it will contain the repository you originally downloaded. Navigate your way to it and 'pull' the changes from GitHub:

```
$ git pull
```
Otherwise – say if you deleted the repository – just pull Shairport Sync from GitHub again and move into the new directory:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
```
(If you wish to use the `development` branch, you should enter the command `$ git checkout development` at this point. Everything else is the same.)

Now, while in the `shairport-sync` directory, perform the following commands (note that there is a choice you must make in there):
```
$ autoreconf -fi
```
Please review the release notes to see if any configuration settings have been changed. For instance, in the transitions from version 2 to version 3, the `--with-ssl=polarssl` has been deprecated in favour of `--with-ssl=mbedtls`.
```
#The following is the standard configuration for a Linux that uses the alsa backend and systemd initialisation system:
$ ./configure --with-alsa --with-avahi --with-ssl=openssl --with-metadata --with-soxr --with-systemd --sysconfdir=/etc
#OR
#The following is the standard configuration for a Linux that uses the alsa backend and the older System V initialisation system:
$ ./configure --with-libdaemon --with-alsa --with-avahi --with-ssl=openssl --with-metadata --with-soxr --with-systemv --sysconfdir=/etc

$ make
$ sudo make install
```
At this point you have downloaded, compiled and installed the updated Shairport Sync. However, the older version is still running. So, you need to do a little more: 

If you are on a `systemd`-based system such as Raspbian Jessie or recent versions of Ubuntu and Debian, execute the following commands:
```
$ sudo systemctl daemon-reload
$ sudo systemctl restart shairport-sync
```
Otherwise execute the following command:
```
$ sudo service shairport-sync restart
```

Your Shairport Sync should be upgraded now. 

### Post Update Tasks
1 **Unmute the Output Device Mixer** (This applies only if you are using a hardware mixer for volume control.) Certain older versions of Shairport Sync could leave the output device mixer in a muted state after use to minimise the possibility of noise. However, this is not generally compatible with other audio players using the same device, as they would generally expect the device to be unmuted. Recent versions of Shairport Sync therefore do not use the mute facility by default. When you update Shairport Sync, the output device mixer might have been muted by the previous version, so you should unmute it, using, for instance, the following command:
```
$ sudo amixer sset <mixer_name> unmute
```
Alternatively you can use `alsamixer`. A muted output has the letter(s) `M` as its value. Select it and type `M` again to unmute. 

