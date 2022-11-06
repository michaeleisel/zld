
build: fetch
	misc/checks.rb
	xcodebuild -project ld/zld.xcodeproj -scheme zld -derivedDataPath build -configuration Release build

build_homebrew: fetch
	misc/checks.rb
	xcodebuild -project ld/zld.xcodeproj -scheme zld -derivedDataPath build -configuration Release build ONLY_ACTIVE_ARCH=YES

test: fetch
	xcodebuild -project ld/zld.xcodeproj -scheme unit-tests -derivedDataPath build -configuration Debug build

clean:
	rm -rf build cfe-8.0.1.src dyld-733.6 dyld-940 llvm-8.0.1.src pstl llvm-13.0.1.src tapi-1100.0.11 tbb

dyld-940:
	git clone --depth=1 https://github.com/apple-oss-distributions/dyld.git $@
	patch -p1 -d $@ < patches/dyld.patch

fetch: dyld-940 llvm-13.0.1.src tapi-1100.0.11 tbb

llvm-13.0.1.src:
	curl -# -L https://github.com/llvm/llvm-project/releases/download/llvmorg-13.0.1/llvm-13.0.1.src.tar.xz | tar xJ

HASH := $(shell git rev-parse --short HEAD)
package:
	tar -C build/Build/Products/Release -czvf build/Build/Products/Release/zld.$(HASH).tar.gz zld
	tar -C build/Build/Products/Release -czvf build/Build/Products/Release/zld.dSYM.$(HASH).tar.gz zld.dSYM

github_release: build
	cd build/Build/Products/Release && zip -r zld.zip zld

tapi-1100.0.11:
	mkdir -p $@
	curl -# -L https://github.com/apple-oss-distributions/tapi/archive/tapi-1100.0.11.tar.gz | tar xz -C $@ --strip-components=1
	patch -p1 -d $@ < patches/tapi.patch

tbb:
	curl -# -L https://github.com/oneapi-src/oneTBB/releases/download/v2020.3/tbb-2020.3-mac.tgz | tar xz

install: build
	mkdir -p "/usr/local/bin"
	cp -f "build/Build/Products/Release/zld" "/usr/local/bin/zld"

