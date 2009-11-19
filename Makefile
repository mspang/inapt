all: magic awesome awesome.png
magic: magic.cc
	g++ -o magic -g3 -Wall -Werror -lapt-pkg -lapt-inst magic.cc acqprogress.cc
awesome: awesome.c acqprogress.cc
	g++ -o awesome -g3 -Wall -Werror -lapt-pkg -lapt-inst awesome.c
awesome.c: awesome.rl
	ragel awesome.rl
awesome.dot: awesome.rl
	ragel -V awesome.rl -o awesome.dot
awesome.png: awesome.dot
	dot -Tpng -o awesome.png awesome.dot

clean:
	rm -f magic

