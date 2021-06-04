build:
	scripts/build.rb

test: fetch
	xcodebuild -project ld/zld.xcodeproj -scheme unit-tests -derivedDataPath build -configuration Debug build

clean:

HASH := $(shell git rev-parse --short HEAD)
package:
	tar -C build/Build/Products/Release -czvf build/Build/Products/Release/zld.$(HASH).tar.gz zld
	tar -C build/Build/Products/Release -czvf build/Build/Products/Release/zld.dSYM.$(HASH).tar.gz zld.dSYM

github_release: build
	tar -C build/Build/Products/Release -czvf build/Build/Products/Release/zld.tar.gz zld

install: build
	mkdir -p "/usr/local/bin"
	cp -f "build/Build/Products/Release/zld" "/usr/local/bin/zld"
