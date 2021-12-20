# Original AirPlay 1 Only Docker Image

## Basic Usage

```
$ docker run -d --restart unless-stopped --net host --device /dev/snd \
    mikebrady/shairport-sync
```
The above command will run Shairport Sync as a daemon in a Docker container, accessing the computer's ALSA audio infrastructure. It will send audio to the default output device and make no use of any hardware mixers the default device might have. The AirPlay service name will be the host's `hostname` with the first letter capitalised, e.g. `Ubuntu`.

## Options

Any options you add to the command above will be passed to Shairport Sync. Here is an example:
```
$ docker run -d --restart unless-stopped --net host --device /dev/snd \
    mikebrady/shairport-sync -a DenSystem -- -d hw:0 -c PCM
```
This will sent audio to alsa hardware device `hw:0` and make use of the that device's mixer control called `PCM`. The service will be visible as `DenSystem` on the network.

## Configuration File

Edit the configuration file `/etc/shairport-sync.conf` in the container (or use the `-v` option to mirror an external copy of `shairport-sync.conf` in to `/etc/shairport-sync.conf`) to get access to the full range of configuration options.