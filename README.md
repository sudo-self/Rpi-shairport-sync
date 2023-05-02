# Shairport Sync
## Shairport Sync does not support AirPlay video or photo streaming.
![2](https://user-images.githubusercontent.com/119916323/235683100-e9f4253e-2e97-4eb6-ae0a-a79aec5317f5.jpg)
![1](https://user-images.githubusercontent.com/119916323/235683159-96d834bd-38b9-48f4-8921-d8e139756458.JPG)

# Quick Start
## apt install --no-install-recommends build-essential git autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev \
    libplist-dev libsodium-dev libavutil-dev libavcodec-dev libavformat-dev uuid-dev libgcrypt-dev xxd
## systemctl enable avahi-daemon
## systemctl start avahi-daemon
## git clone https://github.com/mikebrady/shairport-sync.git
## cd shairport-sync
## autoreconf -fi
##  ./configure --sysconfdir=/etc --with-alsa \
>     --with-soxr --with-avahi --with-ssl=openssl --with-systemd --with-airplay-2
## make && make install
## git clone https://github.com/mikebrady/nqptp.git && cd nqptp 
## autoreconf -fi
## ./configure --with-systemd-startup
## make && make install 
## systemctl start nqptp
## systemctl restart shairport-sync
## shairport-sync
## iwconfig wlan0 power off
## shairport-sync -d
## shairport-sync -v
## systemctl enable shairport-sync
## sudo reboot

* A building guide is available [here](BUILD.md).
* Next Steps and Advanced Topics are [here](ADVANCED%20TOPICS/README.md).
![2](https://user-images.githubusercontent.com/119916323/235680254-d8875bc7-0cd4-47d0-b0e3-081d8ed331af.jpg)
![1](https://user-images.githubusercontent.com/119916323/235680066-75291086-8d8a-46bd-80e9-25e32671621c.JPG)

# Status
Shairport Sync was designed to [run best](ADVANCED%20TOPICS/GetTheBest.md) on stable, dedicated, stand-alone low-power "headless" systems with ALSA as the audio system and with a decent CD-quality Digital to Analog Converter (DAC).




