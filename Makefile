CXX = g++
CXXFLAGS = -std=c++14 -Wall -o
LIBS = -lpthread

main:

	$(CXX) ./src/main.cpp $(CXXFLAGS) ZooKeeper $(LIBS)

clean:

	-rm -f ZooKeeper



