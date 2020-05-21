Fedora Installation Guide
-----

Install the toolchain and pre-requisites, if necessary:
```
# yum install make automake gcc gcc-c++ kernel-devel
# yum install alsa-lib-devel autoconf automake avahi-devel libconfig-devel \
    libdaemon-devel openssl-devel popt-devel soxr-devel rpmbuild
```
Download the tarball from the "releases" tab on github or use `wget` and then use `rpmbuild`. Alternatively, download a tarball of the `master` branch, as in this example:
```
$ wget -O shairport-sync-master.tar.gz https://github.com/mikebrady/shairport-sync/archive/master.tar.gz
$ rpmbuild -ta shairport-sync-master.tar.gz
```
The `-ta` means "build all from this tarball".

The RPM will be built in a directory and will have a pathname like, for example, `~/rpmbuild/RPMS/i686/shairport-sync-2.6-1.fc22.i686.rpm` You should then install it with (for this example):
```
# rpm -i ~/rpmbuild/RPMS/i686/shairport-sync-2.6-1.fc22.i686.rpm
```
You may have to manually create the directory `/var/shairport-sync` for the installation to succeed. Having edited the configuration file `/etc/shairport-sync.conf` as appropriate (see ("Configuring Shairport Sync")[https://github.com/mikebrady/shairport-sync/blob/master/README.md#configuring-shairport-sync]), enable and start the service with:
```
# systemctl enable shairport-sync.service
# systemctl start shairport-sync.service
```
