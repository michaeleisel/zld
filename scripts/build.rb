#!/usr/bin/env ruby

require 'fileutils'

def sh!(cmd)
  raise unless system(cmd)
end

def abseil(arch, target)
  FileUtils.remove_dir("absl") if File.directory?("absl")
  sh!("mkdir absl")
  sh!("curl -# -L https://github.com/abseil/abseil-cpp/archive/refs/tags/20210324.2.tar.gz | tar xz -C absl")
  sh!("mkdir absl/build")
  sh!("cmake -DCMAKE_OSX_ARCHITECTURES=#{arch} -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -S absl/abseil-cpp-20210324.2 -B absl/build")
  sh!("make -C absl/build -j")
  sh!("find absl/build/absl -name '*.a' | xargs libtool -static -o #{target}")
end

def tbb(arch, target)
  FileUtils.remove_dir("tbb") if File.directory?("tbb")
  sh!("mkdir tbb")
  sh!("curl -# -L https://github.com/intel/tbb/archive/v2020.1.tar.gz | tar xz -C tbb --strip-components=1")
  tbb_arch = arch == "x86_64" ? "intel64" : "arm64"
  sh!("make -C tbb -j arch=#{tbb_arch} extra_inc=big_iron.inc")
  sh!("find tbb/build -name libtbb.a | xargs lipo -create -output #{target}")
end

def build_libs(archs)
  ["arm64", "x86_64"].each do |arch|
    abseil(arch, "absl/absl_#{arch}.a")
    tbb(arch, "tbb/tbb_#{arch}.a")
  end
  sh!("lipo -create tbb/tbb_*.a -output ld/tbb.a")
  sh!("lipo -create absl/absl_*.a -output ld/absl.a")
end

def build(universal)
  raise "cmake not found in system path" unless system('which cmake > /dev/null')
  if universal
    build_libs(["arm64", "x86_64"])
    sh!("xcodebuild -project ld/zld.xcodeproj -scheme zld -derivedDataPath build -configuration Release build")
  else
    arch = `uname -m`.chomp
    build_libs([arch])
    sh!("xcodebuild -project ld/zld.xcodeproj -scheme zld -derivedDataPath build -configuration Release build ONLY_ACTIVE_ARCH=YES")
  end
end

build(true)
