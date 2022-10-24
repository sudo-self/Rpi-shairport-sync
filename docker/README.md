# Shairport Sync Docker Image

Available at: https://hub.docker.com/r/mikebrady/shairport-sync

Versions with tags including `-classic` provide classic AirPlay, the same as Shairport Sync versions up to 3.3.9. 

(**Note:** At this time, `stable` tags may not be available.)

## Example Docker Compose File
See the `docker-compose.yaml` file in this folder for an example.

## Docker Run

To run the latest stable release of Shairport Sync, which provides AirPlay 2 service:

```
$ docker run -d --restart unless-stopped --net host --device /dev/snd \
    mikebrady/shairport-sync:stable
```
To run the classic version:

```
$ docker run -d --restart unless-stopped --net host --device /dev/snd \
    mikebrady/shairport-sync:stable-classic
```

### Options

Command line options will be passed to Shairport Sync. Here is an example:
```
$ docker run -d --restart unless-stopped --net host --device /dev/snd \
    mikebrady/shairport-sync:stable \
    -v --statistics -a DenSystem -d hw:0 -c PCM
```
This will send audio to alsa hardware device `hw:0` and make use of the that device's mixer control called `PCM`. The service will be visible as `DenSystem` on the network.

## Configuration File

To get access to the full range of configuration options, pass the configuration file to `/etc/shairport-sync.conf` in the container using the `-v` option or docker compose.

## Building
### Build Example (for arm7 devices)

```
docker buildx build --platform linux/arm/v7 -f ./docker/Dockerfile --build-arg SHAIRPORT_SYNC_BRANCH=development --build-arg NQPTP_BRANCH=development --no-cache -t shairport-sync:development .
```

`SHAIRPORT_SYNC_BRANCH` and `NQPTP_BRANCH` are required to ensure the image is built using the expected branch.
`--no-cache` needs to be used to force buildx to pull the NQPTP branch for new updates. This slows down the build time though so can be removed when it is not beneficial during testing.

### "Classic" AirPlay

The "Classic" AirPlay only dockerfile is in the `classic` folder. This also includes the `start.sh` script used by the container.

### GitHub Action Builds

Requires the following secrets to be set in the repo:
- `DOCKER_REGISTRY` - docker.io if using Docker Hub, else set to your registry URL.
- `DOCKER_REGISTRY_TOKEN` - Access token for your registry.
- `DOCKER_REGISTRY_USER` - Login user for your registry.
- `DOCKER_IMAGE_NAME` - The name of the image, for example `your-registry.com/shairport-sync` or just `your-username/shairport-sync` if using Docker Hub.
