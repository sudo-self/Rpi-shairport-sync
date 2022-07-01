Fedora Installation Guide [Needs updating for Airplay 2]
-----

Update everything...
```
# yum update
```
Install the toolchain and prerequisites, if necessary:
```
# yum install make automake gcc gcc-c++ kernel-devel
# yum install alsa-lib-devel autoconf automake avahi-devel libconfig-devel \
    openssl-devel popt-devel soxr-devel rpmbuild
```
For AirPlay 2, you'll also need to install the following prerequisites:
```
# yum install ffmpeg ffmpeg-devel libplist-devel libsodium-devel libgcrypt-devel libuuid-devel vim-common
```
Download the tarball from the "releases" tab on github or use `wget` and then use `rpmbuild`. Alternatively, download a tarball of the `master` branch, as in this example:
```
$ wget -O shairport-sync-master.tar.gz https://github.com/mikebrady/shairport-sync/archive/master.tar.gz
$ rpmbuild -ta shairport-sync-master.tar.gz
```
The `-ta` means "build all from this tarball".

The RPM will be built in a directory and will have a pathname like, for example, `~/rpmbuild/RPMS/i686/shairport-sync-2.6-1.fc22.i686.rpm` You should then install it with (for this example):
```
$ sudo rpm -i ~/rpmbuild/RPMS/i686/shairport-sync-2.6-1.fc22.i686.rpm
```
You may have to manually create the directory `/var/shairport-sync` beforehand for the installation to succeed. Having edited the configuration file `/etc/shairport-sync.conf` as appropriate (see [Configuring Shairport Sync](https://github.com/mikebrady/shairport-sync/blob/master/README.md#configuring-shairport-sync)), enable and start the service with:
```
# systemctl enable shairport-sync.service
# systemctl start shairport-sync.service
```
