all:
	gcc frcloud.c jsonparser.c -lgnutls -lcurl -lreadline -o frcloud

clean:
	rm frcloud
	chmod -x *.h *.c
