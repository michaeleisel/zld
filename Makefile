
build: fetch
	misc/checks.rb
	xcodebuild -project ld/zld.xcodeproj -scheme zld -derivedDataPath build -configuration Release build

test: fetch
	xcodebuild -project ld/zld.xcodeproj -scheme unit-tests -derivedDataPath build -configuration Debug build

abseil-cpp-20200225:
	curl -# -L https://github.com/abseil/abseil-cpp/archive/20200225.tar.gz | tar xz
	mkdir $@/build
	cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -S $@ -B $@/build
	make -C $@/build -j
	find $@/build/absl -name '*.a' | xargs libtool -static -o $@/build/libabsl.a

cfe-8.0.1.src:
	curl -# -L https://github.com/llvm/llvm-project/releases/download/llvmorg-8.0.1/cfe-8.0.1.src.tar.xz | tar xJ

clean:
	rm -rf abseil-cpp-20200225 build cfe-8.0.1.src dyld-733.6 llvm-8.0.1.src pstl tapi-1100.0.11 tbb

dyld-733.6:
	curl -# -L https://opensource.apple.com/tarballs/dyld/dyld-733.6.tar.gz | tar xz
	patch -p1 -d dyld-733.6 < patches/dyld.patch

fetch: abseil-cpp-20200225 cfe-8.0.1.src dyld-733.6 llvm-8.0.1.src tapi-1100.0.11 tbb

llvm-8.0.1.src:
	curl -# -L https://github.com/llvm/llvm-project/releases/download/llvmorg-8.0.1/llvm-8.0.1.src.tar.xz | tar xJ

HASH := $(shell git rev-parse --short HEAD)
package:
	tar -C build/Build/Products/Release -cvJf build/Build/Products/Release/zld.$(HASH).tar.xz zld
	tar -C build/Build/Products/Release -cvJf build/Build/Products/Release/zld.dSYM.$(HASH).tar.xz zld.dSYM

tapi-1100.0.11:
	mkdir -p $@
	curl -# -L https://opensource.apple.com/tarballs/tapi/tapi-1100.0.11.tar.gz | tar xz -C $@ --strip-components=1
	patch -p1 -d $@ < patches/tapi.patch

tbb:
	curl -# -L https://github.com/intel/tbb/releases/download/v2020.1/tbb-2020.1-mac.tgz | tar xz

install: build
	mkdir -p "/usr/local/bin"
	cp -f "build/Build/Products/Release/zld" "/usr/local/bin/zld"

