# Shairport Sync for Apple streaming on Raspberry pi4
## <img width="487" alt="Screenshot 2023-05-02 at 7 44 59 AM" src="https://user-images.githubusercontent.com/119916323/235685570-51b0568c-6bce-4fad-8775-13b57a2b6673.png">
## Shairport Sync does not support AirPlay video or photo streaming.
# Quick Start
### apt install --no-install-recommends build-essential git autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev \
    libplist-dev libsodium-dev libavutil-dev libavcodec-dev libavformat-dev uuid-dev libgcrypt-dev xxd
### systemctl enable avahi-daemon
### systemctl start avahi-daemon
### git clone https://github.com/mikebrady/shairport-sync.git
### cd shairport-sync
### autoreconf -fi
###  ./configure --sysconfdir=/etc --with-alsa \
>     --with-soxr --with-avahi --with-ssl=openssl --with-systemd --with-airplay-2
### make && make install
### git clone https://github.com/mikebrady/nqptp.git && cd nqptp 
### autoreconf -fi
### ./configure --with-systemd-startup
### make && make install 
### systemctl start nqptp
### systemctl restart shairport-sync
### shairport-sync
### iwconfig wlan0 power off
### shairport-sync -d
### shairport-sync -v
### systemctl enable shairport-sync
### sudo reboot
* A building guide is available [here](BUILD.md).
* Next Steps and Advanced Topics are [here](ADVANCED%20TOPICS/README.md).





