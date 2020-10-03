
build: fetch
	xcodebuild -project ld/zld.xcodeproj -scheme zld -derivedDataPath build -configuration Release build

test: fetch
	xcodebuild -project ld/zld.xcodeproj -scheme unit-tests -derivedDataPath build -configuration Debug build

abseil-cpp-20200225:
	curl -# -L https://github.com/abseil/abseil-cpp/archive/20200225.tar.gz | tar xz
	mkdir $@/build
	cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -S $@ -B $@/build
	make -C $@/build -j
	find $@/build/absl -name '*.a' | xargs libtool -static -o $@/build/libabsl.a

cfe-7.0.1.src:
	curl -# -L http://releases.llvm.org/7.0.1/cfe-7.0.1.src.tar.xz | tar xJ

clean:
	rm -rf abseil-cpp-20200225 build cfe-7.0.1.src dyld-635.2 llvm-7.0.1.src pstl tapi-b920569 tbb

dyld-635.2:
	curl -# -L https://opensource.apple.com/tarballs/dyld/dyld-635.2.tar.gz | tar xz
	patch -p1 -d dyld-635.2 < patches/dyld.patch

fetch: abseil-cpp-20200225 cfe-7.0.1.src dyld-635.2 llvm-7.0.1.src tapi-b920569 tbb

llvm-7.0.1.src:
	curl -# -L http://releases.llvm.org/7.0.1/llvm-7.0.1.src.tar.xz | tar xJ

HASH := $(shell git rev-parse --short HEAD)
package:
	tar -C build/Build/Products/Release -cvJf build/Build/Products/Release/zld.$(HASH).tar.xz zld
	tar -C build/Build/Products/Release -cvJf build/Build/Products/Release/zld.dSYM.$(HASH).tar.xz zld.dSYM

tapi-b920569:
	mkdir -p $@
	curl -# -L https://github.com/ributzka/tapi/tarball/b920569 | tar xz -C $@ --strip-components=1
	patch -p1 -d $@ < patches/tapi.patch

tbb:
	curl -# -L https://github.com/intel/tbb/releases/download/v2020.1/tbb-2020.1-mac.tgz | tar xz

install: build
	mkdir -p "/usr/local/bin"
	cp -f "build/Build/Products/Release/zld" "/usr/local/bin/zld"

