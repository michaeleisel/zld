#!/usr/bin/env ruby

Dir.chdir(__dir__)

file = ARGV.size >= 1 ? ARGV[0] : "/tmp/results-ld"
lines = IO.read(file).split("\n")
whitelist = nil
arch = nil
fails = []
lines.each do |line|
  arch_match = /^* * * Running all unit tests for architecture (\S+)/.match(line)
  if arch_match
    arch = arch_match[1]
    whitelist = IO.read("#{arch}_whitelist").split("\n")
    next
  end
  test_match = /^(\S+)/.match(line)
  if test_match
    test = test_match[1]
    next if whitelist.include?(test)
    fails << [arch, test]
  end
end

if fails.size > 0
  puts "Failures:"
  puts fails.map { |f| f.join(": ") }.join("\n")
  exit 1
end
