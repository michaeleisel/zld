require 'pry-byebug'

IO.read("link_cmds").split("\n").drop(1).each do |cmd|
  dir = cmd.match(/^cd (.*?) && /)[1]
  out = cmd.match(/ -o (\S+)/)[1]
  path = "#{dir}/#{out}"
  system("#{cmd} -fuse-ld=/Library/Developer/CommandLineTools/usr/bin/ld")
   `mv #{path} /tmp/#{out}`
  #expected = IO.read(path).bytes
  system("#{cmd} -fuse-ld=/Users/michael/Library/Developer/Xcode/DerivedData/ld64-dzjqukslgnudkbewsvkxtjueagwq/Build/Products/Release/ld")
  success = system("cmp #{path} /tmp/#{out}")
  if out.include?("..")
    puts "#### #{out}"
  else
    if success
      puts "pass"
    else
      binding.pry 
    end
  end
=begin
  actual = IO.read(path).bytes
  success = false
  if expected.size == actual.size
    puts "1"
    z = expected.lazy.zip(actual)
    puts "2"
    i = 0
    diffs = z.each do |a, b|
      binding.pry if a != b
      i += 1
      ""
    end
    binding.pry
    if diffs <= 4
      success = true
    end
  end
  if !success
    binding.pry
    ""
  end
=end
end

=begin
def match(str, regex)
  m = str.match(regex)
  m ? m[1] : ""
end

lines = IO.read("firefox_build").split("\n").map { |line| match(line, /^\s*\S+\s*(.*)/) }

root = "/Users/michael/projects/mozilla/mozilla-unified/obj-x86_64-apple-darwin18.5.0/"

lines.each_with_index do |line, idx|
  next unless line.match(/^\S+clang/) && line.include?("-Wl,")
  #next unless line.match(/ -o \S*Test[a-zA-Z]*(\s|^)/)
  #next unless line.include?(" -o TestArrayUtils ")
  path_idx = (idx-1).downto(0).detect do |j|
    #binding.pry
    path = "#{root}/#{lines[j]}"
    !path.include?(" ") && (File.exist?(path) || File.exist?(File.dirname(path)))
  end
  dir = File.dirname("#{root}/#{lines[path_idx]}")
  Dir.chdir(dir)
  success = system("#{line} 2>/dev/null")
  puts "cd #{dir} && #{line}" if success
  # puts "#{path} #{line}"
  ""
end
=end
