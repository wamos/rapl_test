CFLAGS = -O2 -g
CXXFLAGS = $(CFLAGS)
CC = gcc
CXX = g++
##LIBS_PAPI = -lpapi
LDFLAGS = -Wl,-z,now

BINARY_TARGETS = msr-poll-gaps-nsec msr-poll-gaps-nsec-and-power test_rapl

all: $(BINARY_TARGETS)

clean:
	rm -f $(BINARY_TARGETS)

.PHONY: all clean

test_rapl: test_rapl.cpp
	 $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lm

msr-poll-gaps-nsec: msr-poll-gaps-nsec.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lrt

msr-poll-gaps-nsec-and-power: msr-poll-gaps-nsec-and-power.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lrt
