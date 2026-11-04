# Plain clang build of CS35L56Amp.kext (macOS host with Xcode CLT).
# make SDK=/path/to/MacKernelSDK [FWDIR=build/fw]
SDK    ?= MacKernelSDK
FWDIR  ?= build/fw
OUT    ?= build
KEXT   := $(OUT)/CS35L56Amp.kext
VER    ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)
CXX    := xcrun clang++
ARCH   := -arch x86_64 -mmacosx-version-min=11.0
CFLAGS := $(ARCH) -std=gnu++17 -O2 -g -Wall -Wno-unused-parameter -Werror=return-type \
          -fno-rtti -fno-exceptions -fno-builtin -fno-common -mkernel -fapple-kext \
          -nostdinc -nostdinc++ -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -D__MACH__ \
          -DCS35L56_VERSION=\"$(VER)\" -I$(SDK)/Headers -Isrc
CCKEXT := $(shell xcrun clang -print-resource-dir)/lib/darwin/libclang_rt.cc_kext.a
LDFLAGS:= $(ARCH) -nostdlib -Xlinker -kext -L$(SDK)/Library/x86_64 -lkmod -lkmodc++ $(CCKEXT)
SRCS   := src/Cs35l56.cpp src/CsDsp.cpp src/MtlGpio.cpp src/LpssSpi.cpp src/CS35L56Amp.cpp src/HdaDump.cpp $(OUT)/FirmwareData.cpp
OBJS   := $(patsubst %.cpp,$(OUT)/obj/%.o,$(notdir $(SRCS))) $(OUT)/obj/kmod_info.o

all: $(KEXT)

$(OUT)/FirmwareData.cpp: scripts/fw2c.py
	sh scripts/fetch_fw.sh $(FWDIR)
	python3 -I scripts/fw2c.py $(FWDIR) $@

$(OUT)/obj/%.o: src/%.cpp src/*.h src/*.hpp
	@mkdir -p $(OUT)/obj
	$(CXX) $(CFLAGS) -c $< -o $@

$(OUT)/obj/kmod_info.o: src/kmod_info.c
	@mkdir -p $(OUT)/obj
	xcrun clang $(ARCH) -O2 -mkernel -fno-builtin -nostdinc -DKERNEL -DKERNEL_PRIVATE -DAPPLE -DNeXT -D__MACH__ -I$(SDK)/Headers -c $< -o $@

$(OUT)/obj/FirmwareData.o: $(OUT)/FirmwareData.cpp src/Firmware.h
	@mkdir -p $(OUT)/obj
	$(CXX) $(CFLAGS) -c $< -o $@

$(KEXT): $(OBJS) Info.plist
	rm -rf $(KEXT)
	mkdir -p $(KEXT)/Contents/MacOS
	$(CXX) $(LDFLAGS) $(OBJS) -o $(KEXT)/Contents/MacOS/CS35L56Amp
	sed -e 's/<string>0.1.0-dev<\/string>/<string>0.1.0-$(VER)<\/string>/' Info.plist > $(KEXT)/Contents/Info.plist
	-cp $(FWDIR)/LICENSE.cirrus $(KEXT)/Contents/ 2>/dev/null
	nm -u $(KEXT)/Contents/MacOS/CS35L56Amp | head -80
	plutil -lint $(KEXT)/Contents/Info.plist
	@v=$$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' $(KEXT)/Contents/Info.plist); \
	echo "$$v" | grep -Eq '^[0-9]+(\.[0-9]+){0,2}$$' || { echo "CFBundleVersion '$$v' is rejected by XNU"; exit 1; }

clean:
	rm -rf $(OUT)/obj $(KEXT)

.PHONY: all clean
