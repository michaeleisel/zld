#!/usr/bin/env ruby

raise "Building from source not currently supported for arm64, please use prebuilt instead" if `uname -m`.chomp == "arm64"
raise "cmake not found in system path" unless system('which cmake')
