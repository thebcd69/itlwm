# Host test of the portable core. FW = directory holding the linux-firmware files.
FW ?= ../build/fw
CXX ?= g++
SRC = ../src/Cs35l56.cpp ../src/CsDsp.cpp host_test.cpp
host_test: $(SRC) ../src/*.h
	$(CXX) -std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -o $@ $(SRC)
run: host_test
	./host_test $(FW)/CS35L56_Rev3.11.16.wmfw $(FW)/cs35l56-b0-dsp1-misc-10431c63-spkid0-amp1.bin \
	  $(FW)/cs35l56-b0-dsp1-misc-10431c63-spkid0-amp2.bin $(FW)/cs35l56-b0-dsp1-misc-10431c63-spkid1-amp1.bin \
	  $(FW)/cs35l56-b0-dsp1-misc-10431c63-spkid1-amp2.bin
.PHONY: run
