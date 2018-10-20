all: a1jobs.cpp a1mon.cpp
	g++ -Wall -std=c++11 -o a1jobs a1jobs.cpp
	g++ -Wall -std=c++11 -o a1mon a1mon.cpp



clean:
	rm -f a1jobs a1mon

tar:
	tar -cf submit.tar a1jobs.cpp a1mon.cpp design-document.pdf Makefile

