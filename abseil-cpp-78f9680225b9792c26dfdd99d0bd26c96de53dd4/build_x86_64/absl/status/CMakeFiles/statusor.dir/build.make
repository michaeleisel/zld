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
CMAKE_BINARY_DIR = /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64

# Include any dependencies generated for this target.
include absl/status/CMakeFiles/statusor.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include absl/status/CMakeFiles/statusor.dir/compiler_depend.make

# Include the progress variables for this target.
include absl/status/CMakeFiles/statusor.dir/progress.make

# Include the compile flags for this target's objects.
include absl/status/CMakeFiles/statusor.dir/flags.make

absl/status/CMakeFiles/statusor.dir/statusor.cc.o: absl/status/CMakeFiles/statusor.dir/flags.make
absl/status/CMakeFiles/statusor.dir/statusor.cc.o: ../absl/status/statusor.cc
absl/status/CMakeFiles/statusor.dir/statusor.cc.o: absl/status/CMakeFiles/statusor.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object absl/status/CMakeFiles/statusor.dir/statusor.cc.o"
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64/absl/status && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT absl/status/CMakeFiles/statusor.dir/statusor.cc.o -MF CMakeFiles/statusor.dir/statusor.cc.o.d -o CMakeFiles/statusor.dir/statusor.cc.o -c /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/absl/status/statusor.cc

absl/status/CMakeFiles/statusor.dir/statusor.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/statusor.dir/statusor.cc.i"
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64/absl/status && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/absl/status/statusor.cc > CMakeFiles/statusor.dir/statusor.cc.i

absl/status/CMakeFiles/statusor.dir/statusor.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/statusor.dir/statusor.cc.s"
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64/absl/status && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/absl/status/statusor.cc -o CMakeFiles/statusor.dir/statusor.cc.s

# Object files for target statusor
statusor_OBJECTS = \
"CMakeFiles/statusor.dir/statusor.cc.o"

# External object files for target statusor
statusor_EXTERNAL_OBJECTS =

absl/status/libabsl_statusor.a: absl/status/CMakeFiles/statusor.dir/statusor.cc.o
absl/status/libabsl_statusor.a: absl/status/CMakeFiles/statusor.dir/build.make
absl/status/libabsl_statusor.a: absl/status/CMakeFiles/statusor.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX static library libabsl_statusor.a"
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64/absl/status && $(CMAKE_COMMAND) -P CMakeFiles/statusor.dir/cmake_clean_target.cmake
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64/absl/status && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/statusor.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
absl/status/CMakeFiles/statusor.dir/build: absl/status/libabsl_statusor.a
.PHONY : absl/status/CMakeFiles/statusor.dir/build

absl/status/CMakeFiles/statusor.dir/clean:
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64/absl/status && $(CMAKE_COMMAND) -P CMakeFiles/statusor.dir/cmake_clean.cmake
.PHONY : absl/status/CMakeFiles/statusor.dir/clean

absl/status/CMakeFiles/statusor.dir/depend:
	cd /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64 && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4 /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/absl/status /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64 /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64/absl/status /Users/meisel/projects/zld/abseil-cpp-78f9680225b9792c26dfdd99d0bd26c96de53dd4/build_x86_64/absl/status/CMakeFiles/statusor.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : absl/status/CMakeFiles/statusor.dir/depend

