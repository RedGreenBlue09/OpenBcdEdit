cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0069 NEW) 
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

macro(set_build_dir_default TARGET_NAME_)
	set_target_properties(${TARGET_NAME_}
		PROPERTIES
		ARCHIVE_OUTPUT_DIRECTORY "Binary"
		LIBRARY_OUTPUT_DIRECTORY "Library"
		RUNTIME_OUTPUT_DIRECTORY "Binary"
	)
endmacro()

project(OpenBcdEdit LANGUAGES C)

add_subdirectory("${OpenBcdEdit_SOURCE_DIR}/Library/OpenBcd")

set(OpenBcdEdit_HEADER_FILES
)
set(OpenBcdEdit_SOURCE_FILES
	"${OpenBcdEdit_SOURCE_DIR}/Source/Main.c"
)

add_executable(OpenBcdEdit
	${OpenBcdEdit_HEADER_FILES}
	${OpenBcdEdit_SOURCE_FILES}
)
target_include_directories(OpenBcdEdit
	PUBLIC  "${OpenBcdEdit_SOURCE_DIR}/Include"
)
target_link_libraries(OpenBcdEdit
	PRIVATE OpenBcd
)
set_target_properties(OpenBcdEdit PROPERTIES
	INTERPROCEDURAL_OPTIMIZATION TRUE
	C_STANDARD 99
)

set_build_dir_default(OpenBcdEdit)
