CPPFLAGS := -g3 -O2 -Wall -Werror
LDFLAGS  := -Wl,--as-needed
INCLUDES := $(shell krb5-config --cflags)
override CFLAGS  += -std=gnu99 $(INCLUDES)

all: inapt parser.png

inapt: inapt.o parser.o acqprogress.o
	g++ -o inapt -g3 -Wall -Werror -lapt-pkg -lapt-inst $^

parser.cc: parser.rl
	ragel parser.rl -o parser.cc

parser.dot: parser.rl
	ragel -V parser.rl -o parser.dot

parser.png: parser.dot
	dot -Tpng -o parser.png parser.dot

clean:
	rm -f *.o inapt parser.png parser.dot

