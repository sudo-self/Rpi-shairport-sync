Installing to Cygwin
----

This is based on installing onto a fresh default installation of Cygwin 2.4.1 (64-bit installation) running in Windows 10
inside VMWare Fusion on a Mac. 

Note: to enable Cygwin to resolve domain names, it was found necessary to go to Windows `Control Panel` > `Network and Internet` > `Network Connections` >
`Ethernet0` > `Properties` > `Internet Protocol Version 4 (TCP/IPv4)` > `Properties`, select `Use the following DNS server addresses`
and enter valid DNS server addresses, e.g. `8.8.8.8`.

* Use `Cygwin Setup` to install the following packages:
  * `pkg-config`
  * `autoconf`
  *  `automake`
  *  `clang`
  *  `libdaemon-devel`
  *  `popt-devel`
  *  `make`
  *  `libao-devel`
  *  `openssl-devel`
  *  `libtool`
  *  `git`
  * `wget` for convenience,
  * `flex` for compiling `libconfig`
  * `bison` for compiling `libconfig` 
  
Assuming the Cygwin setup program (`setup-x86_64.exe` in this case) is in your Downloads directory and the default directory is your home directory, the following command should work:
```
.\Downloads\setup-x86_64.exe -P pkg-config,autoconf,automake,clang,libdaemon-devel,popt-devel,make,libao-devel,openssl-devel,libtool,git,wget,flex,bison
```

* Download, configure, compile and install `libconfig`:
```
$ git clone https://github.com/hyperrealm/libconfig.git
$ ./make
$ autoreconf -fi
$ ./configure
$ make
$ make install
$ cd ..
```
* Next, download, configure and compile Shairport Sync:
```
$ cd 
$ cd shairport-sync
$ autoreconf -fi
$ PKG_CONFIG_PATH=/usr/local/lib/pkgconfig ./configure --with-ao --with-ssl=openssl --with-tinysvcmdns
$ make
$ make install
```
* That's it. There should be a `shairport-sync.exe` file in your directory.
