magic: magic.cc
	g++ -o magic -g3 -Wall -Werror -lapt-pkg -lapt-inst magic.cc
clean:
	rm -f magic

