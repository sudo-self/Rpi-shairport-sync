#!/bin/sh

set -e

rm -rf /var/run/dbus.pid
#mkdir -p /var/run/dbus

dbus-uuidgen --ensure
dbus-daemon --system

avahi-daemon --daemonize --no-chroot

# TODO: This should probably be implemented in a better way and using the correct nqptp group.
nqptp &

# su-exec shairport-sync shairport-sync $@

shairport-sync -vu --statistics