CXX = g++
CXXFLAGS = -std=c++17
CLSPVFLAGS = -cl-std=CL2.0 -inline-entry-points

.PHONY: clean easyvk

all: build easyvk runner

build:
	mkdir -p build

clean:
	rm -r build

easyvk: easyvk/src/easyvk.cpp easyvk/src/easyvk.h
	$(CXX) $(CXXFLAGS) -Ieasyvk/src -c easyvk/src/easyvk.cpp -o build/easyvk.o

kernel: runner.cpp 
	$(CXX) $(CXXFLAGS) -Ieasyvk/src build/easyvk.o runner.cpp -lvulkan -o build/runner

%.spv: %.cl
	clspv -w -cl-std=CL2.0 -inline-entry-points $< -o build/$@