ifdef BUILD_PROFILE
	FORCE_BUILD_PROFILE=__NOT_EXIST__
else
	FORCE_BUILD_PROFILE=.current_profile.mk
	-include .current_profile.mk
endif

ifndef BUILD_PROFILE
	BUILD_PROFILE=default_build_profile.conf
endif

all : all_debug all_release
all_debug: build/debug/Makefile
	@$(MAKE) --no-print-directory -C build/debug all
all_release: build/release/Makefile
	@$(MAKE) --no-print-directory -C build/release all
clean:
	@$(MAKE) --no-print-directory -C build/debug clean
	@$(MAKE) --no-print-directory -C build/release clean
install:
	@$(MAKE) --no-print-directory -C build/release install
test:
	@$(MAKE) --no-print-directory -C build/release test

$(FORCE_BUILD_PROFILE):
	echo $(FORCE_BUILD_PROFILE)
	$(file >.current_profile.mk,BUILD_PROFILE=$(BUILD_PROFILE))

build/debug/Makefile:  $(BUILD_PROFILE) $(FORCE_BUILD_PROFILE) | build/debug/conf build/debug/log build/debug/data
	mkdir -p build/debug/log
	rm -f build/debug/CMakeCache.txt
	cmake -G "Unix Makefiles" -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug `grep -E -v "^[[:blank:]]*#" $(BUILD_PROFILE)`

build/release/Makefile: $(BUILD_PROFILE) $(FORCE_BUILD_PROFILE) | build/release/conf build/release/log build/release/data
	mkdir -p build/release/log
	rm -f build/release/CMakeCache.txt
	cmake -G "Unix Makefiles" -S . -B build/release -DCMAKE_BUILD_TYPE=Release `grep -E -v "^[[:blank:]]*#" $(BUILD_PROFILE)`

build/debug/conf: | build/debug conf 
	cd build/debug; ln -s ../../conf conf
build/release/conf: | build/release conf 
	cd build/release; ln -s ../../conf conf

build/debug/data: | build/debug data 
	cd build/debug; ln -s ../../data data
build/release/data: | build/release data 
	cd build/release; ln -s ../../data data

build/debug/log: | build/debug
	mkdir build/debug/log

build/release/log: | build/release
	mkdir build/release/log

build/debug:
	@mkdir -p build/debug

build/release:
	@mkdir -p build/release

conf:
	@mkdir -p conf

data:
	@mkdir -p data

distclean:
	rm -rfv build
