dir = "/Users/michael/Library/Developer/Xcode/DerivedData/zld-bjskpeidtiujmgcpwkcpcjfjafhe/Build/Products"
files = `ls #{dir}/Release/*`.split("\n").select { |file| File.file?(file) }

Dir.chdir("#{__dir__}/../ld")#/unit-tests")
`rm -r build`
`mkdir build`
path = Dir.pwd
Dir.chdir("build")
["Debug", "Release-assert", "Release"].each do |name|
  `mkdir #{name}`
  files.each do |file|
    dest = "#{Dir.pwd}/#{name}/#{File.basename(file)}"
    `ln -s #{file} #{dest}`
    if File.basename(file) == "zld"
      `ln -s #{file} #{dest[0..-4] + "ld"}`
    end
  end
end
