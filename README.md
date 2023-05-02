# Shairport Sync for Apple streaming on Raspberry pi4
## Shairport Sync does not support AirPlay video or photo streaming.
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

### ![2](https://user-images.githubusercontent.com/119916323/235684196-fa20faff-b189-437e-9733-0f908eab071b.png)

### ![1](https://user-images.githubusercontent.com/119916323/235684301-56048f28-7e7c-4555-9497-e255457e08dd.png)

* A building guide is available [here](BUILD.md).
* Next Steps and Advanced Topics are [here](ADVANCED%20TOPICS/README.md).

# Status
Shairport Sync was designed to [run best](ADVANCED%20TOPICS/GetTheBest.md) on stable, dedicated, stand-alone low-power "headless" systems with ALSA as the audio system and with a decent CD-quality Digital to Analog Converter (DAC).




