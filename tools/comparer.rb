#!/usr/bin/env ruby

require 'pry-byebug'

binary = "helix"
b1 = "bin-#{binary}-1"
b2 = "bin-#{binary}-2"

c1 = IO.binread(b1)
c2 = IO.binread(b2)

c1_size = `size -xlm #{b1}`.split("\n")
c2_size = `size -xlm #{b2}`.split("\n")
if c1_size != c2_size
  puts "size mismatch"
  binding.pry
end
sections = c1_size.map do |l|
  m = l.match(/^\tSection (\S+): (\S+) \S+ \S+ offset (\S+)\)$/)
  next nil unless m
  name, size, start = m[1..-1]
  [name, start.to_i, size.to_i(16)]
end.compact

segments = c1_size.map do |l|
  m = l.match(/^Segment (\S+): (\S+) \S+ \S+ fileoff (\S+)\)$/)
  next nil unless m
  name, size, start = m[1..-1]
  [name, start.to_i, size.to_i(16)]
end.compact

diffs = sections.select { |name, start, size| c1[start...(start+size)] != c2[start...(start+size)] }
exp_diffs = [["__objc_methlist", 214759432, 959484],
 ["__objc_const", 318577248, 10202424],
 ["__objc_selrefs", 328779672, 237120],
 ["__llvm_covfun", 0, 0]
]

linkedit_seg = segments.detect { |name, _| name == "__LINKEDIT" }
if diffs.map(&:first) - exp_diffs.map(&:first) != []
  puts "Not equal, diff sections:\n#{diffs}"
else
  bytes_diff = `./a #{b1} #{b2} #{linkedit_seg[1]} #{linkedit_seg[2]}`.to_i
  if bytes_diff >= 1500
    puts "Not equal, bytes diff:\n#{bytes_diff}"
  else
    puts "equal enough"
  end
end

nil
