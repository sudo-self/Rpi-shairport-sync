shairport-sync.7: shairport-sync.7.xml
	xmltoman shairport-sync.7.xml > shairport-sync.7

shairport-sync.html: shairport-sync.7.xml
	xsltproc xmltoman.xsl shairport-sync.7.xml > shairport-sync.html

all: shairport-sync.7 shairport-sync.html

clean:
	rm shairport-sync.7
	rm shairport-sync.html
