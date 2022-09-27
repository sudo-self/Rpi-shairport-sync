# Metadata

Shairport Sync can deliver metadata supplied by the source, such as Album Name, Artist Name, Cover Art, etc.
through a pipe or UDP socket to a recipient application program — see https://github.com/mikebrady/shairport-sync-metadata-reader for a sample recipient.
Sources that supply metadata include iTunes and the Music app in macOS and iOS.


## Metadata over UDP

As an alternative to sending metadata to a pipe, the `socket_address` and `socket_port` tags may be set in the metadata group to cause Shairport Sync
to broadcast UDP packets containing the track metadata.

The advantage of UDP is that packets can be sent to a single listener or, if a multicast address is used, to multiple listeners.
It also allows metadata to be routed to a different host. However UDP has a maximum packet size of about 65000 bytes; while large enough for most data, Cover Art will often exceed this value. Any metadata exceeding this limit will not be sent over the socket interface. The maximum packet size may be set with the `socket_msglength` tag to any value between 500 and 65000 to control this - lower values may be used to ensure that each UDP packet is sent in a single network frame. The default is 500. Other than this restriction, metadata sent over the socket interface is identical to metadata sent over the pipe interface.

The UDP metadata format is very simple - the first four bytes are the metadata *type*, and the next four bytes are the metadata *code*
(both are sent in network byte order - see https://github.com/mikebrady/shairport-sync-metadata-reader for a definition of those terms).
The remaining bytes of the packet, if any, make up the raw value of the metadata.

