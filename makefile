INCLUDE = -Iinclude/ -I.
LIBINCLUDE = -Iinclude/lib/
CXX = g++ -g -pipe -O2 -std=c++20 -pedantic -Wextra -Wall -Wno-maybe-uninitialized -Wno-unused-function

rwildcard=$(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

LIBSRCS = $(call rwildcard,src/,*.cpp)
LIBOBJS = $(LIBSRCS:src/%.cpp=build/%.o)
LIBDEPS = $(LIBOBJS:%.o=%.d)

#$(info -----------------------------------------------)
#$(info LIBSRCS - ${LIBSRCS})
#$(info -----------------------------------------------)
#$(info LIBOBJS - ${LIBOBJS})
#$(info -----------------------------------------------)
#$(info LIBDEPS - ${LIBDEPS})
#$(info -----------------------------------------------)

.PHONY: default
default: build/libsimbroker.so

.PHONY: test
test: build/test
	@echo "----- Begin Tests -----"
	@build/test

.PHONY: mkTestData
mkTestData: build/mkTestData

build/:
	mkdir -p build

build/libsimbroker.so: build/ $(LIBOBJS)
	$(CXX) $(INCLUDE) $(LIBINCLUDE) -shared -o build/libsimbroker.so $(LIBOBJS)

-include $(LIBDEPS)

build/%.o: src/%.cpp
	mkdir -p $(@D)
	$(CXX) -fPIC -MMD -c $(INCLUDE) $(LIBINCLUDE) $< -o $@

build/test: test/test.cpp build/libsimbroker.so
	$(CXX) $(INCLUDE) test/test.cpp -o build/test build/libsimbroker.so

build/mkTestData: test/mkTestData.cpp
	$(CXX) $(INCLUDE) test/mkTestData.cpp -o build/mkTestData -lalpacaclient -lpqxx -lssl -lcrypto

.PHONY: clean
clean:
	rm -rf build/*
