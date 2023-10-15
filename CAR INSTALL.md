# Shairport Sync for Cars
If your car audio has an AUX input, you can get AirPlay in your car using Shairport Sync. Together, Shairport Sync and an iPhone or an iPad with cellular capability can give you access to internet radio, YouTube, Apple Music, Spotify, etc. on the move. While Shairport Sync is no substitute for CarPlay, the audio quality is often much better than Bluetooth.

## The Basic Idea

The basic idea is to use a small Linux computer to create an isolated WiFi network (a "car network") and run Shairport Sync on it to provide an AirPlay service. An iPhone or an iPad with cellular capability can simultaneously connect to internet radio, YouTube, Apple Music, Spotify, etc. over the cellular network and send AirPlay audio through the car network to the AirPlay service provided by Shairport Sync. This sends the audio to the computer's DAC which is connected to the AUX input of your car audio.

Note that Android devices can not, so far, do this trick of using the two networks simultaneously.

## Example

**Important Note** This guide can not be used for the latest (time of writing: October 2023) version of Rasberry Pi OS (Bookworm) because the `dhcpd` package is no longer part of the system. It does work for Rasberry Pi OS (Bullseye)

In this example, a Raspberry Pi Zero 2 W and a Pimoroni PHAT DAC are used. Shairport Sync will be built for AirPlay 2 operation, but you can build it for "classic" AirPlay (aka AirPlay 1) operation if you prefer. A Pi Zero W is powerful enough for classic AirPlay.

Please note that some of the details of setting up networks are specific to the version of Linux used.

### Prepare the initial SD Image
* Download Raspberry Pi OS Bullseye (Lite) and install it onto an SD Card using `Raspberry Pi Imager`. The Lite version is preferable to the Desktop version as it doesn't include a sound server like PulseAudio or PipeWire that can prevent direct access to the audio output device.
* Before writing the image to the card, use the Settings control on `Raspberry Pi Imager` to set hostname, enable SSH and provide a username and password to use while building the system. Similarly, you can specify a wireless network the Pi will connect to while building the system. Later on, the Pi will be configured to start its own isolated network.
* The next few steps are to add the overlay needed for the sound card. This may not be necessary in your case, but in this example a Pimoroni PHAT is being used. If you do not need to add an overlay, skip these steps.
  * Mount the card on a Linux machine. Two drives should appear – a `boot` drive and a `rootfs` drive.
  * `cd` to the `boot` drive (since my username is `mike`, it will be `$ cd /media/mike/boot`).
  * Edit the `config.txt` file to add the overlay needed for the sound card. This may not be necessary in your case, but in this example a Pimoroni PHAT is being used and it needs the following entry to be added:
    ```
    dtoverlay=hifiberry-dac
    ```
  * Close the file and carefully dismount and eject the two drives. *Be sure to dismount and eject the drives properly; otherwise they may be corrupted.*
* Remove the SD card from the Linux machine, insert it into the Pi and reboot.

After a short time, the Pi should appear on your network – it may take a minute or so. To check, try to `ping` it at the `<hostname>.local`, e.g. if the hostname is `bmw` then use `$ ping bmw.local`. Once it has appeared, you can SSH into it and configure it.

### Boot, Configure, Update 
The first thing to do on a Pi would be to use the `raspi-config` tool to expand the file system to use the entire card. Next, do the usual update and upgrade:
```
# apt-get update
# apt-get upgrade
``` 

### Build and Install 
Let's get the tools and libraries for building and installing Shairport Sync (and NQPTP).

```
# apt install --no-install-recommends build-essential git xmltoman autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev \
    libplist-dev libsodium-dev libavutil-dev libavcodec-dev libavformat-dev uuid-dev libgcrypt-dev xxd
```
If you are building classic Shairport Sync, the list of packages is shorter:
```
# apt-get install --no-install-recommends build-essential git xmltoman autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev
```

#### NQPTP
Skip this section if you are building classic Shairport Sync – NQPTP is not needed for classic Shairport Sync.

Download, install, enable and start NQPTP from [here](https://github.com/mikebrady/nqptp) following the guide for Linux.

#### Shairport Sync
Download Shairport Sync, check out the `development` branch and configure, compile and install it.

* Omit the `--with-airplay-2` from the `./configure` options if you are building classic Shairport Sync.

```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ autoreconf -fi
$ ./configure --sysconfdir=/etc --with-alsa \
    --with-soxr --with-avahi --with-ssl=openssl --with-systemd --with-airplay-2
$ make
# make install
```
The `autoreconf` step may take quite a while – please be patient!

**Note:** *Do not* enable Shairport Sync to start automatically at boot time – later on in this installation, we will arrange for it to start after the network has been set up.

### Configure Shairport Sync
Here are the important options for the Shairport Sync configuration file at `/etc/shairport-sync.conf`:
```
// Sample Configuration File for Shairport Sync for Car Audio with a Pimoroni PHAT
general =
{
	name = "BMW Radio";
	ignore_volume_control = "yes";
	volume_max_db = -3.00;
};

alsa =
{
	output_device = "hw:1"; // the name of the alsa output device. Use "alsamixer" or "aplay" to find out the names of devices, mixers, etc.
};

```
Two `general` settings are worth noting. First, the option to ignore the sending device's volume control is enabled. This means that the car audio's volume control is the only one that affects the audio volume. Of course this is a matter of personal preference.
Second, the maximum output offered by the DAC to the AUX port of the car audio can be reduced if it is overloading the input circuits. Again, that's a matter for personal selection and adjustment.

The `alsa` settings are for the Pimoroni PHAT – it does not have a hardware mixer, so no `mixer_control_name` is given.

Note that the DAC's 32-bit capability is automatically selected if available, so there is no need to set it here. Similarly, since `soxr` support is included in the build, `soxr` interpolation will be automatically enabled if the device is fast enough.

### Extra Packages
A number of packages to enable the Pi to work as a WiFi base station are needed:
```
# apt-get install hostapd isc-dhcp-server
```
Disable both of these services from starting at boot time (this is because we will launch them sequentially later on):
```
# systemctl unmask hostapd
# systemctl disable hostapd
# systemctl disable isc-dhcp-server
```
#### Configure HostAPD
Configure `hostapd` by creating `/etc/hostapd/hostapd.conf` with the following contents which will set up an open network with the name BMW. You might wish to change the name:
``` 
# Thanks to https://wiki.gentoo.org/wiki/Hostapd#802.11b.2Fg.2Fn_triple_AP

# The interface used by the AP
interface=wlan0

# This is the name of the network -- yours may be different
ssid=BMW

# "g" simply means 2.4GHz band
hw_mode=g

# Channel to use
channel=11

# Limit the frequencies used to those allowed in the country
ieee80211d=1

# The country code
country_code=IE

# Enable 802.11n support
ieee80211n=1

# QoS support, also required for full speed on 802.11n/ac/ax
wmm_enabled=1

```
Note that, since the car network is isolated from the Internet, you don't really need to secure it with a password.

#### Configure DHCP server

First, replace the contents of `/etc/dhcp/dhcpd.conf` with this:
```
subnet 10.0.10.0 netmask 255.255.255.0 {
     range 10.0.10.5 10.0.10.150;
     #option routers <the-IP-address-of-your-gateway-or-router>;
     #option broadcast-address <the-broadcast-IP-address-for-your-network>;
}
```
Second, modify the `INTERFACESv4` entry at the end of the file `/etc/default/isc-dhcp-server` to look as follows:
```
INTERFACESv4="wlan0"
INTERFACESv6=""
```
### Set up the Startup Sequence
Configure the startup sequence by adding commands to `/etc/rc.local` to start `hostapd` and the `dhcp` server and then to start `shairport-sync` automatically after startup. Its contents should look like this:
```
#!/bin/sh -e
#
# rc.local
#
# This script is executed at the end of each multiuser runlevel.
# Make sure that the script will "exit 0" on success or any other
# value on error.
#
# In order to enable or disable this script just change the execution
# bits.
#
# By default this script does nothing.

/sbin/iw dev wlan0 set power_save off
/usr/sbin/hostapd -B -P /run/hostapd.pid /etc/hostapd/hostapd.conf
/sbin/ip addr add 10.0.10.1/24 dev wlan0
/bin/sleep 1
/bin/systemctl start isc-dhcp-server
/bin/sleep 2
/bin/systemctl start shairport-sync

exit 0

```
As you can see, the effect of these commands is to start the WiFi transmitter, give the base station the IP address `10.0.10.1`, start a DHCP server and finally start the Shairport Sync service.

### Final Steps
Up to now, if you reboot the Pi, it will reconnect to your WiFi network, ignoring the instructions and settings you have given it to act as a base station. That is because the `wlan0` interface is still under the control of the `dhcpcd` service. So, the final step is to instruct the `dhcpcd` service not to manage `wlan0`. To do this, edit `/etc/dhcpcd.conf` and insert the following line at the start:
```
denyinterfaces wlan0
```
From this point on, at least on the Raspberry Pi, if you reboot the machine, it will not reconnect to your network. Instead, it will act as the WiFi base station you have configured with `hostapd` and `isc-dhcp-server`.

### Optimise startup time – Raspberry Pi Specific

This is applicable to a Raspberry Pi only. Some of it may be applicable to other systems, but it has not been tested on them.

There are quite a few services that are not necessary for this setup. Disabling them can improve startup time. Running these commands disables them:

```
sudo systemctl disable systemd-timesyncd.service
sudo systemctl disable keyboard-setup.service
sudo systemctl disable triggerhappy.service
sudo systemctl disable dhcpcd.service
sudo systemctl disable wpa_supplicant.service
sudo systemctl disable dphys-swapfile.service
sudo systemctl disable networking.service
```

### Read-only mode – Raspberry Pi Specific
Run `sudo raspi-config` and then choose `Performance Options` > `Overlay Filesystem` and choose to enable the overlay filesystem, and to set the boot partition to be write-protected. 

### Ready
Install the Raspberry Pi in your car. It should be powered from a source that is switched off when you leave the car, otherwise the slight current drain will eventually flatten the car's battery.

When the power source is switched on, typically when you start the car, it will take maybe a minute for the system to boot up.

### Enjoy!

---

## Updating
From time to time, you may wish to update this installation. However, in order to update Shairport Sync, you must reconnect the system to a network that can access the internet. The easiest thing is to temporarily reconnect to the network you used when you created the system. To do that, you have to temporarily undo the "Final Steps" and some of the "Raspberry Pi Specific" steps you used. This will enable you to connect your device back to the network it was created on. You should then be able to update the operating system and libraries in the normal way and then update Shairport Sync.

So, take the following steps:

1. If it's a Raspberry Pi and you have enabled the Read-only mode, you must take the device out of Read-only mode:  
Run `sudo raspi-config` and then choose `Performance Options` > `Overlay Filesystem` and choose to disable the overlay filesystem and to set the boot partition not to be write-protected. This is so that changes can be written to the file system; you can make the filesystem read-only again later. Save the changes and reboot the system.

2. If you have disabled the `dhcpcd`, `wpa_supplicant` or `systemd-timesyncd` services as suggested in the "Optimise startup time -- Raspberry Pi Specific" section, you need to temporarily re-enable them:  
`# systemctl enable dhcpcd.service`  
`# systemctl enable wpa_supplicant.service`  
`# systemctl enable systemd-timesyncd.service`  
Reboot.

3. To allow your device to reconnect to the network it was created on, edit `/etc/dhcpcd.conf` and comment out the following line at the start:  
`denyinterfaces wlan0`  
so that it looks like this:  
`# denyinterfaces wlan0`  
From this point on, if you reboot the machine, it will connect to the network it was configured on, i.e. the network you used when you set it up for the first time. This is because the name and password of the network it was created on would have been placed in `/etc/wpa_supplicant/wpa_supplicant` when the system was initially configured and will still be there.

4. Reboot and do Normal Updating

   You can perform updates in the normal way. When you are finished, you need to undo the temporary changes you made to the setup, as follows:

5. If you had temporarily re-enabled services that are normally disabled, then it's time to disable them again:  
`# systemctl disable dhcpcd.service`  
`# systemctl disable wpa_supplicant.service`  
`# systemctl disable systemd-timesyncd.service`  

6. To re-enable the system to create its own network, edit `/etc/dhcpcd.conf` and uncomment the line that you had temporarily commented out at the start of the update. Change:  
`# denyinterfaces wlan0`  
so that it looks like this:  
`denyinterfaces wlan0`  

7. Reboot. The system should start as it would if it was in the car.

8. If the device is a Raspberry Pi and you wish to make the file system read-only, connect to the system, run `sudo raspi-config` and then choose `Performance Options` > `Overlay Filesystem`. In there, choose to enable the overlay filesystem, and to set the boot partition to be write-protected. Do a final reboot and check that everyting is in order.
