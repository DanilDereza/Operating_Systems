# CMAKE generated file: DO NOT EDIT!
# Generated by "MinGW Makefiles" Generator, CMake Version 3.31

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

SHELL = cmd.exe

# The CMake executable.
CMAKE_COMMAND = "C:\Program Files\CMake\bin\cmake.exe"

# The command to remove a file.
RM = "C:\Program Files\CMake\bin\cmake.exe" -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = C:\Users\danil\Desktop\4_course\OS\Lab_2

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = C:\Users\danil\Desktop\4_course\OS\Lab_2\build

# Include any dependencies generated for this target.
include CMakeFiles/process_manager.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/process_manager.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/process_manager.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/process_manager.dir/flags.make

CMakeFiles/process_manager.dir/codegen:
.PHONY : CMakeFiles/process_manager.dir/codegen

CMakeFiles/process_manager.dir/src/process_manager.cpp.obj: CMakeFiles/process_manager.dir/flags.make
CMakeFiles/process_manager.dir/src/process_manager.cpp.obj: CMakeFiles/process_manager.dir/includes_CXX.rsp
CMakeFiles/process_manager.dir/src/process_manager.cpp.obj: C:/Users/danil/Desktop/4_course/OS/Lab_2/src/process_manager.cpp
CMakeFiles/process_manager.dir/src/process_manager.cpp.obj: CMakeFiles/process_manager.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=C:\Users\danil\Desktop\4_course\OS\Lab_2\build\CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/process_manager.dir/src/process_manager.cpp.obj"
	C:\MinGW\bin\c++.exe $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/process_manager.dir/src/process_manager.cpp.obj -MF CMakeFiles\process_manager.dir\src\process_manager.cpp.obj.d -o CMakeFiles\process_manager.dir\src\process_manager.cpp.obj -c C:\Users\danil\Desktop\4_course\OS\Lab_2\src\process_manager.cpp

CMakeFiles/process_manager.dir/src/process_manager.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/process_manager.dir/src/process_manager.cpp.i"
	C:\MinGW\bin\c++.exe $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E C:\Users\danil\Desktop\4_course\OS\Lab_2\src\process_manager.cpp > CMakeFiles\process_manager.dir\src\process_manager.cpp.i

CMakeFiles/process_manager.dir/src/process_manager.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/process_manager.dir/src/process_manager.cpp.s"
	C:\MinGW\bin\c++.exe $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S C:\Users\danil\Desktop\4_course\OS\Lab_2\src\process_manager.cpp -o CMakeFiles\process_manager.dir\src\process_manager.cpp.s

# Object files for target process_manager
process_manager_OBJECTS = \
"CMakeFiles/process_manager.dir/src/process_manager.cpp.obj"

# External object files for target process_manager
process_manager_EXTERNAL_OBJECTS =

libprocess_manager.dll: CMakeFiles/process_manager.dir/src/process_manager.cpp.obj
libprocess_manager.dll: CMakeFiles/process_manager.dir/build.make
libprocess_manager.dll: CMakeFiles/process_manager.dir/linkLibs.rsp
libprocess_manager.dll: CMakeFiles/process_manager.dir/objects1.rsp
libprocess_manager.dll: CMakeFiles/process_manager.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --bold --progress-dir=C:\Users\danil\Desktop\4_course\OS\Lab_2\build\CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX shared library libprocess_manager.dll"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles\process_manager.dir\link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/process_manager.dir/build: libprocess_manager.dll
.PHONY : CMakeFiles/process_manager.dir/build

CMakeFiles/process_manager.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles\process_manager.dir\cmake_clean.cmake
.PHONY : CMakeFiles/process_manager.dir/clean

CMakeFiles/process_manager.dir/depend:
	$(CMAKE_COMMAND) -E cmake_depends "MinGW Makefiles" C:\Users\danil\Desktop\4_course\OS\Lab_2 C:\Users\danil\Desktop\4_course\OS\Lab_2 C:\Users\danil\Desktop\4_course\OS\Lab_2\build C:\Users\danil\Desktop\4_course\OS\Lab_2\build C:\Users\danil\Desktop\4_course\OS\Lab_2\build\CMakeFiles\process_manager.dir\DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/process_manager.dir/depend

