
abseil-cpp-20200225:
	curl -# -L https://github.com/abseil/abseil-cpp/archive/20200225.tar.gz | tar xz

cfe-7.0.1.src:
	curl -# http://releases.llvm.org/7.0.1/cfe-7.0.1.src.tar.xz | tar xJ

clean:
	rm -rf abseil-cpp-20200225 cfe-7.0.1.src dyld-635.2 llvm-7.0.1.src pstl tapi-b920569 tbb

dyld-635.2:
	curl -# https://opensource.apple.com/tarballs/dyld/dyld-635.2.tar.gz | tar xz
	patch -p1 -d dyld-635.2 < patches/dyld.patch

fetch: abseil-cpp-20200225 cfe-7.0.1.src dyld-635.2 llvm-7.0.1.src tapi-b920569 tbb

llvm-7.0.1.src:
	curl -# http://releases.llvm.org/7.0.1/llvm-7.0.1.src.tar.xz | tar xJ

tapi-b920569:
	mkdir -p $@
	curl -# -L https://github.com/ributzka/tapi/tarball/b920569 | tar xz -C $@ --strip-components=1
	patch -p1 -d $@ < patches/tapi.patch

tbb:
	curl -# -L https://github.com/intel/tbb/releases/download/v2020.1/tbb-2020.1-mac.tgz | tar xz

