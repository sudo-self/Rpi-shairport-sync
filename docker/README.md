# Shairport Sync Docker Image

Available at: https://hub.docker.com/r/mikebrady/shairport-sync

## Original AirPlay 1 Image
Please see: [airplay1/README.md](airplay1/README.md)

## GitHub Action Builds
Requires the following secrets to be set in the repo:
- `DOCKER_REGISTRY` - docker.io if using Docker Hub, else set to your registry URL.
- `DOCKER_REGISTRY_TOKEN` - Access token for your registry.
- `DOCKER_REGISTRY_USER` - Login user for your registry.
- `DOCKER_IMAGE_NAME` - The name of the image, for example `your-registry.com/shairport-sync` or just `your-username/shairport-sync` if using Docker Hub.