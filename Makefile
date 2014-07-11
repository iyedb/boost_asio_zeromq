# Makefile for asio_zsock
# expects boost and zeromq
#

BOOST_LFLAGS = -lboost_system
ZMQ_LFLAGS = -lzmq
CXX_FLAGS = -std=c++11

asio_zsock: asio_zsock.o
	g++ -g $(CXX_FLAGS) $(BOOST_LFLAGS) $(ZMQ_LFLAGS) -o asio_zsock asio_zsock.o

HEADERS = $(wildcard *.h) $(wildcard *.hpp)

%.o: %.cpp $(HEADERS)
	g++ $(CXX_FLAGS) -c $<

%.o: %.cxx $(HEADERS)
	g++ -g $(CXX_FLAGS) -c $<

all: asio_zsock

clean:
	rm -f *.o
	rm -f asio_zsock
