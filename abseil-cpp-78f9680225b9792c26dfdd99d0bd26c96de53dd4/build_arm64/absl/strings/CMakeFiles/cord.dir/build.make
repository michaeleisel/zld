# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.23

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /opt/homebrew/Cellar/cmake/3.23.1/bin/cmake

# The command to remove a file.
RM = /opt/homebrew/Cellar/cmake/3.23.1/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64

# Include any dependencies generated for this target.
include absl/strings/CMakeFiles/cord.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include absl/strings/CMakeFiles/cord.dir/compiler_depend.make

# Include the progress variables for this target.
include absl/strings/CMakeFiles/cord.dir/progress.make

# Include the compile flags for this target's objects.
include absl/strings/CMakeFiles/cord.dir/flags.make

absl/strings/CMakeFiles/cord.dir/cord.cc.o: absl/strings/CMakeFiles/cord.dir/flags.make
absl/strings/CMakeFiles/cord.dir/cord.cc.o: ../absl/strings/cord.cc
absl/strings/CMakeFiles/cord.dir/cord.cc.o: absl/strings/CMakeFiles/cord.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object absl/strings/CMakeFiles/cord.dir/cord.cc.o"
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/absl/strings && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT absl/strings/CMakeFiles/cord.dir/cord.cc.o -MF CMakeFiles/cord.dir/cord.cc.o.d -o CMakeFiles/cord.dir/cord.cc.o -c /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/absl/strings/cord.cc

absl/strings/CMakeFiles/cord.dir/cord.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/cord.dir/cord.cc.i"
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/absl/strings && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/absl/strings/cord.cc > CMakeFiles/cord.dir/cord.cc.i

absl/strings/CMakeFiles/cord.dir/cord.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/cord.dir/cord.cc.s"
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/absl/strings && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/absl/strings/cord.cc -o CMakeFiles/cord.dir/cord.cc.s

absl/strings/CMakeFiles/cord.dir/cord_analysis.cc.o: absl/strings/CMakeFiles/cord.dir/flags.make
absl/strings/CMakeFiles/cord.dir/cord_analysis.cc.o: ../absl/strings/cord_analysis.cc
absl/strings/CMakeFiles/cord.dir/cord_analysis.cc.o: absl/strings/CMakeFiles/cord.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building CXX object absl/strings/CMakeFiles/cord.dir/cord_analysis.cc.o"
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/absl/strings && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT absl/strings/CMakeFiles/cord.dir/cord_analysis.cc.o -MF CMakeFiles/cord.dir/cord_analysis.cc.o.d -o CMakeFiles/cord.dir/cord_analysis.cc.o -c /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/absl/strings/cord_analysis.cc

absl/strings/CMakeFiles/cord.dir/cord_analysis.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/cord.dir/cord_analysis.cc.i"
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/absl/strings && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/absl/strings/cord_analysis.cc > CMakeFiles/cord.dir/cord_analysis.cc.i

absl/strings/CMakeFiles/cord.dir/cord_analysis.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/cord.dir/cord_analysis.cc.s"
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/absl/strings && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/absl/strings/cord_analysis.cc -o CMakeFiles/cord.dir/cord_analysis.cc.s

# Object files for target cord
cord_OBJECTS = \
"CMakeFiles/cord.dir/cord.cc.o" \
"CMakeFiles/cord.dir/cord_analysis.cc.o"

# External object files for target cord
cord_EXTERNAL_OBJECTS =

absl/strings/libabsl_cord.a: absl/strings/CMakeFiles/cord.dir/cord.cc.o
absl/strings/libabsl_cord.a: absl/strings/CMakeFiles/cord.dir/cord_analysis.cc.o
absl/strings/libabsl_cord.a: absl/strings/CMakeFiles/cord.dir/build.make
absl/strings/libabsl_cord.a: absl/strings/CMakeFiles/cord.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Linking CXX static library libabsl_cord.a"
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/absl/strings && $(CMAKE_COMMAND) -P CMakeFiles/cord.dir/cmake_clean_target.cmake
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/absl/strings && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/cord.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
absl/strings/CMakeFiles/cord.dir/build: absl/strings/libabsl_cord.a
.PHONY : absl/strings/CMakeFiles/cord.dir/build

absl/strings/CMakeFiles/cord.dir/clean:
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/absl/strings && $(CMAKE_COMMAND) -P CMakeFiles/cord.dir/cmake_clean.cmake
.PHONY : absl/strings/CMakeFiles/cord.dir/clean

absl/strings/CMakeFiles/cord.dir/depend:
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64 && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4 /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/absl/strings /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64 /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/absl/strings /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_arm64/absl/strings/CMakeFiles/cord.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : absl/strings/CMakeFiles/cord.dir/depend

