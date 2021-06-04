#!/usr/bin/env ruby

require 'fileutils'

def sh!(cmd)
  raise unless system(cmd)
end

def rmdir(dir)
  FileUtils.remove_dir(dir) if File.directory?(dir)
end

def abseil(arch, target)
  rmdir("absl")
  sh!("mkdir absl")
  sh!("curl -# -L https://github.com/abseil/abseil-cpp/archive/refs/tags/20210324.2.tar.gz | tar xz -C absl")
  sh!("mkdir absl/build")
  sh!("cmake -DCMAKE_OSX_ARCHITECTURES=#{arch} -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -S absl/abseil-cpp-20210324.2 -B absl/build")
  sh!("make -C absl/build -j")
  sh!("find absl/build/absl -name '*.a' | xargs libtool -static -o #{target}")
end

def tbb(arch, target)
  rmdir("tbb")
  sh!("mkdir tbb")
  sh!("curl -# -L https://github.com/intel/tbb/archive/v2020.1.tar.gz | tar xz -C tbb --strip-components=1")
  tbb_arch = arch == "x86_64" ? "intel64" : "arm64"
  sh!("make -C tbb -j arch=#{tbb_arch} extra_inc=big_iron.inc")
  sh!("find tbb/build -name libtbb.a | xargs lipo -create -output #{target}")
end

def build_libs(archs)
  archs.each do |arch|
    abseil(arch, "absl/absl_#{arch}.a")
    tbb(arch, "tbb/tbb_#{arch}.a")
  end
  sh!("lipo -create tbb/tbb_*.a -output ld/tbb.a")
  sh!("lipo -create absl/absl_*.a -output ld/absl.a")
end

def fetch_other_pieces
  # cfe
  rmdir("cfe-8.0.1.src")
  sh!("curl -# -L https://github.com/llvm/llvm-project/releases/download/llvmorg-8.0.1/cfe-8.0.1.src.tar.xz | tar xJ")
  # dyld
  rmdir("dyld-733.6")
  sh!("curl -# -L https://opensource.apple.com/tarballs/dyld/dyld-733.6.tar.gz | tar xz")
	sh!("patch -p1 -d dyld-733.6 < patches/dyld.patch")
  # LLVM
  rmdir("llvm-8.0.1.src")
  sh!("curl -# -L https://github.com/llvm/llvm-project/releases/download/llvmorg-8.0.1/llvm-8.0.1.src.tar.xz | tar xJ")
  # TAPI
  rmdir("tapi-1100.0.11")
  sh!("mkdir -p tapi-1100.0.11")
  sh!("curl -# -L https://opensource.apple.com/tarballs/tapi/tapi-1100.0.11.tar.gz | tar xz -C tapi-1100.0.11 --strip-components=1")
  sh!("patch -p1 -d tapi-1100.0.11 < patches/tapi.patch")
end

def build(universal)
  raise "cmake not found in system path" unless system('which cmake > /dev/null')
  fetch_other_pieces()
  arch = `uname -m`.chomp
  if universal
    raise "Universal building not currently supported on arm64. Please download the release binary instead, or else build just for arm with `make build_homebrew`" if arch == "arm64"
    build_libs(["arm64", "x86_64"])
    sh!("xcodebuild -project ld/zld.xcodeproj -scheme zld -derivedDataPath build -configuration Release build")
  else
    build_libs([arch])
    sh!("xcodebuild -project ld/zld.xcodeproj -scheme zld -derivedDataPath build -configuration Release build ONLY_ACTIVE_ARCH=YES")
  end
end

universal = !ARGV.include?("--thin")
build(universal)
