#!/usr/bin/ruby

require 'parallel'
require 'pry-byebug'

n_threads = 8

filelist = "/Users/michael/Library/Developer/Xcode/DerivedData/Signal-eaaymknzhulxsvdxstqxeyotiefd/Build/Intermediates.noindex/Signal.build/Debug-iphonesimulator/Signal.build/Objects-normal/x86_64/Signal.LinkFileList"
files = IO.read(filelist).split("\n")
chunks = files.shuffle.each_slice(files.size / n_threads + 1).to_a

stz = Time.now
times = Parallel.map(chunks.each_with_index) do |chunk, idx|
#times = (chunks.each_with_index).map do |chunk, idx|
  IO.write("#{idx}.txt", chunks[idx].join("\n"))
  cmds = IO.read("ld_small").chomp + " -filelist #{idx}.txt -o #{idx}.dylib -undefined suppress -flat_namespace"
  start = Time.now
  system("time -p ld #{cmds} 2>&1")
  finish = Time.now
  finish - start
end
fiz = Time.now

binding.pry
exit

