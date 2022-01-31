all:
	gcc frcloud.c jsonparser.c config.c -lgnutls -lcurl -lreadline -o frcloud

clean:
	rm frcloud
	chmod -x *.h *.c

strip:
	strip frcloud
