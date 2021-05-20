# Shairport Sync Docker Image

Available at: https://hub.docker.com/r/mikebrady/shairport-sync

## Build Example (for arm7 devices)

TODO: remove `--ssh` when going public as this is only for private repos.
```
docker buildx build --ssh github=$SSH_AUTH_SOCK --platform linux/arm/v7 -f ./docker/Dockerfile --build-arg SHAIRPORT_SYNC_BRANCH=charles-dev -t registry.nova.do/shairport-sync:unstable-development .
```

`SHAIRPORT_SYNC_BRANCH` is required to ensure the image is built using the expected branch.