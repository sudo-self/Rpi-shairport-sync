# Shairport Sync Docker Image

Available at: https://hub.docker.com/r/mikebrady/shairport-sync

## Example Docker Compose File
See the `docker-compose.yaml` file in this folder for an example.

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

## Build Example (for arm7 devices)

```
docker buildx build --platform linux/arm/v7 -f ./docker/Dockerfile --build-arg SHAIRPORT_SYNC_BRANCH=development --build-arg NQPTP_BRANCH=development -t shairport-sync:unstable-development .
```

`SHAIRPORT_SYNC_BRANCH` is required to ensure the image is built using the expected branch.

## AirPlay 1 Only

The AirPlay1 only dockerfile is in the `airplay1` folder. This also includes the `start.sh` script used by the container. Please note that the AirPlay 1 image build via the AirPlay 2 branch does not work with multiple instances on the same hardware.