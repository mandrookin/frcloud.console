all:
	chmod -x *.c *.h
	gcc -s frcloud.c jsonparser.c config.c base64.c -lgnutls -lcurl -lreadline -o frcloud

debug:
	gcc -ggdb frcloud.c jsonparser.c config.c base64.c -lgnutls -lcurl -lreadline -o frcloud

clean:
	rm -f frcloud *.bak
	chmod -x *.h *.c

strip:
	strip frcloud
