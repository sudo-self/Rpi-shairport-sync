FROM alpine:3.12 AS builder

# Classic (aka AirPlay 1) Build

# Check required arguments exist. These will be provided by the Github Action
# Workflow and are required to ensure the correct branches are being used.
ARG SHAIRPORT_SYNC_BRANCH
RUN test -n "$SHAIRPORT_SYNC_BRANCH"

RUN apk -U add \
        git \
        build-base \
        autoconf \
        automake \
        libtool \
        dbus \
        alsa-lib-dev \
        popt-dev \
        mbedtls-dev \
        soxr-dev \
        avahi-dev \
        libconfig-dev \
        mosquitto-dev \
        libsndfile-dev

##### ALAC #####
RUN git clone https://github.com/mikebrady/alac
WORKDIR /alac
RUN autoreconf -i
RUN ./configure
RUN make
RUN make install
WORKDIR /
##### ALAC END #####

##### SPS #####
WORKDIR /shairport-sync
COPY . .
RUN git checkout "$SHAIRPORT_SYNC_BRANCH"
WORKDIR /shairport-sync/build
RUN autoreconf -i ../
RUN ../configure --sysconfdir=/etc --with-alsa --with-soxr --with-avahi --with-ssl=mbedtls \
        --with-metadata --with-dummy --with-pipe --with-dbus-interface \
        --with-stdout --with-mpris-interface --with-mqtt-client \
        --with-apple-alac --with-convolution
RUN make -j $(nproc)
RUN DESTDIR=install make install
WORKDIR /
##### SPS END #####

# Shairport Sync Runtime System
FROM crazymax/alpine-s6:3.12-3.1.1.2

RUN apk -U add \
        alsa-lib \
        dbus \
        popt \
        glib \
        mbedtls \
        soxr \
        avahi \
        avahi-tools \
        libconfig \
        mosquitto \
        libsndfile-dev


# Copy build files.
COPY --from=builder /shairport-sync/build/install/usr/local/bin/shairport-sync /usr/local/bin/shairport-sync
COPY --from=builder /usr/local/lib/libalac.* /usr/local/lib/
COPY --from=builder /shairport-sync/build/install/etc/dbus-1/system.d/shairport-sync-dbus.conf /etc/dbus-1/system.d/
COPY --from=builder /shairport-sync/build/install/etc/dbus-1/system.d/shairport-sync-mpris.conf /etc/dbus-1/system.d/

COPY ./docker/classic/etc/s6-overlay/s6-rc.d /etc/s6-overlay/s6-rc.d
RUN chmod +x /etc/s6-overlay/s6-rc.d/startup/script.sh

# D-Bus and MPRIS services are normally provided by the user
# 'shairport-sync' running the 'shairport-sync' app.
# However, the 'shairport-sync' user is not defined in this Docker image,
# so we get a harmless warning that the user 'shairport-sync' is unknown.
# In the Docker image, D-Bus and MPRIS services are provided by
# the root user running the 'shairport-sync' app.

# Remove anything we don't need.
RUN rm -rf /lib/apk/db/*

ENTRYPOINT [ "/init" ]
