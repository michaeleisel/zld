
build: fetch
	misc/checks.rb
	xcodebuild -project ld/zld.xcodeproj -scheme zld -derivedDataPath build -configuration Release build

build_homebrew: fetch
	misc/checks.rb
	xcodebuild -project ld/zld.xcodeproj -scheme zld -derivedDataPath build -configuration Release build ONLY_ACTIVE_ARCH=YES

test: fetch
	xcodebuild -project ld/zld.xcodeproj -scheme unit-tests -derivedDataPath build -configuration Debug build

abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4:
	curl -# -L https://github.com/abseil/abseil-cpp/archive/78f9680225b9792c26dfdd99d0bd26c96de53dd4.tar.gz | tar xz
	mkdir $@/build $@/build_x86_64 $@/build_arm64
	cmake -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -S $@ -B $@/build_x86_64
	make -C $@/build_x86_64 -j
	find $@/build_x86_64/absl -name '*.a' | xargs libtool -static -o $@/build/libabsl_x86_64.a
	cmake -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -S $@ -B $@/build_arm64
	make -C $@/build_arm64 -j
	find $@/build_arm64/absl -name '*.a' | xargs libtool -static -o $@/build/libabsl_arm64.a
	lipo -create $@/build/libabsl_x86_64.a $@/build/libabsl_arm64.a -output $@/build/libabsl.a

cfe-8.0.1.src:
	curl -# -L https://github.com/llvm/llvm-project/releases/download/llvmorg-8.0.1/cfe-8.0.1.src.tar.xz | tar xJ

clean:
	rm -rf abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4 build cfe-8.0.1.src dyld-733.6 llvm-8.0.1.src pstl tapi-1100.0.11 tbb tbb_staticlib ld/libtbb.a

dyld-733.6:
	curl -# -L https://opensource.apple.com/tarballs/dyld/dyld-733.6.tar.gz | tar xz
	patch -p1 -d dyld-733.6 < patches/dyld.patch

fetch: abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4 cfe-8.0.1.src dyld-733.6 llvm-8.0.1.src tapi-1100.0.11 tbb tbb_staticlib

llvm-8.0.1.src:
	curl -# -L https://github.com/llvm/llvm-project/releases/download/llvmorg-8.0.1/llvm-8.0.1.src.tar.xz | tar xJ

HASH := $(shell git rev-parse --short HEAD)
package:
	tar -C build/Build/Products/Release -czvf build/Build/Products/Release/zld.$(HASH).tar.gz zld
	tar -C build/Build/Products/Release -czvf build/Build/Products/Release/zld.dSYM.$(HASH).tar.gz zld.dSYM

github_release: build
	cd build/Build/Products/Release && zip -r zld.zip zld

tapi-1100.0.11:
	mkdir -p $@
	curl -# -L https://opensource.apple.com/tarballs/tapi/tapi-1100.0.11.tar.gz | tar xz -C $@ --strip-components=1
	patch -p1 -d $@ < patches/tapi.patch

tbb:
	curl -# -L https://github.com/intel/tbb/releases/download/v2020.1/tbb-2020.1-mac.tgz | tar xz

tbb_staticlib:
	mkdir -p $@
	curl -# -L https://github.com/intel/tbb/archive/v2020.1.tar.gz | tar xz -C $@ --strip-components=1
	make -C $@ -j arch=intel64 extra_inc=big_iron.inc
	make -C $@ -j arch=arm64 extra_inc=big_iron.inc
	find $@/build -name libtbb.a | xargs lipo -create -output ld/libtbb.a

install: build
	mkdir -p "/usr/local/bin"
	cp -f "build/Build/Products/Release/zld" "/usr/local/bin/zld"

