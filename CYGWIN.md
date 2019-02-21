Installing to Cygwin
----

This is based on installing onto a fresh default installation of Cygwin 2.895 (64-bit installation) running in Windows 10
inside VMWare Fusion on a Mac.

The end result is Cygwin Shairport Sync, an AirPlay facility for Windows.

The is not, as yet, an installer for Shairport Sync in CYGWIN.

Setting up Windows
====
Set up Windows 10 and install all updates. Next, install the `Bonjour Service`, available from Apple in an installer called "Bonjour Print Services for Windows v2.0.2".

* Download and run `Bonjour Print Services for Windows v2.0.2`
* After accepting conditions, the installer will do a preliminary installion of the Bonjour Service and will then pause, inviting you to install Bonjour Print Services. You can decline to do this, as the Bonjour Service will have been installed.

* Check Bonjour Service is running. In Windows, open the "Services" application and ensure that you can see `Bonjour Services` running.


Setting up Cygwin
====
* Download the Cygwin installer from the [official website](https://cygwin.com/install.html). Save the installer in the Downloads folder.

* Open a Window Command Prompt and enter the following (long!) command:
```
Downloads\setup-x86_64.exe -P cygrunsrv,dbus,avahi,avahi-tools,gnome-keyring,libavahi-client-devel, ^
libglib2.0-devel,openssl,pkg-config,autoconf,automake,clang,libdaemon-devel,popt-devel, ^
make,libao-devel,openssl-devel,libtool,git,wget,flex,bison
```
This will do a complete installation of Cygwin and all necessary packages.
* Set up the D-Bus and Avahi Services:

Open a `Cygwin64 Terminal` window in Administrator mode. Enter the following command:
```
$ messagebus-config
```
Enter `yes` for all queries. Next, open the Windows "Services" application (if it's already open, refresh the screen contents) and look for the `CYGWIN D-Bus system service`. Open it and start it.

Next, open (or return to) a `Cygwin64 Terminal` window in Administrator mode. Enter the following command:
```
$  /usr/sbin/avahi-daemon-config
```
Enter `yes` for all queries. Next, open the Windows "Services" application (if it's already open, refresh the screen contents) and look for the `CYGWIN Avahi service`. Open it and start it.

* Download, configure, compile and install `libconfig`:
```
$ git clone https://github.com/hyperrealm/libconfig.git
$ cd libconfig
$ autoreconf -fi
$ ./configure
$ make
$ make install
$ cd ..
```
* Next, download, configure and compile Shairport Sync:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ autoreconf -fi
$ PKG_CONFIG_PATH=/usr/local/lib/pkgconfig ./configure --with-ao --with-ssl=openssl \
    --with-avahi --with-dbus-interface --sysconfdir=/etc
$ make
```
* That's it. There should be a `shairport-sync.exe` file in your directory.
