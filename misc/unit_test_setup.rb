dir = "/Users/michael/Library/Developer/Xcode/DerivedData/zld-bjskpeidtiujmgcpwkcpcjfjafhe/Build/Products"
files = `ls #{dir}/Release/*`.split("\n")

Dir.chdir("#{__dir__}/../ld/unit-tests")
`rm -r build`
`mkdir build`
path = Dir.pwd
Dir.chdir("build")
["Debug", "Release-assert", "Release"].each do |name|
  `mkdir #{name}`
  files.each do |file|
    `ln -s #{file} #{Dir.pwd}/#{name}/#{File.basename(file)}`
  end
end
