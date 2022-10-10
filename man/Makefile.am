man_MANS = shairport-sync.7

if HAVE_XMLTOMAN
shairport-sync.7: shairport-sync.7.xml
	xmltoman $< > $@
endif

if HAVE_XSLTPROC
all-local: shairport-sync.html

shairport-sync.html: xmltoman.xsl shairport-sync.7.xml 
	xsltproc  $^ > $@
endif
