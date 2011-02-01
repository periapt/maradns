# This is a wrapper that runs "./configure ; make"
all:
	./compile.sh

debug:
	./configure ; make debug

clean:
	./configure ; make clean

uninstall:
	./configure ; make uninstall

install:
	echo Please compile MaraDNS first
