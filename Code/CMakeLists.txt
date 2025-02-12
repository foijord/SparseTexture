cmake_minimum_required (VERSION 3.28)
project(SparseTexture VERSION 2022011 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_VERBOSE_MAKEFILE ON)

add_compile_definitions(
	$<$<CONFIG:Debug>:DEBUG>
	$<$<CXX_COMPILER_ID:GNU>:VK_USE_PLATFORM_XCB_KHR>
	$<$<CXX_COMPILER_ID:MSVC>:VK_USE_PLATFORM_WIN32_KHR>
	$<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>									# no min/max macros in Windows.h
	$<$<CXX_COMPILER_ID:MSVC>:WIN32_LEAN_AND_MEAN>						# don't include Winsock.h
	VK_NO_PROTOTYPES)

add_compile_options(
	$<$<CXX_COMPILER_ID:MSVC>:/W4>										# warning level 4
	$<$<CXX_COMPILER_ID:MSVC>:/WX>										# warnings as errors
	$<$<CXX_COMPILER_ID:MSVC>:/wd26812>									# don't warn about unscoped enums, cannot suppress these from included headers
	$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/Zi>)			# /Zi tells the compiler to put debug info into the OBJ file

add_link_options(
	$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/DEBUG>)			# /DEBUG tells the linker that we want to create a PDB file.

include_directories(${CMAKE_SOURCE_DIR})

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)

set(TARGET SparseTexture)
add_executable(${TARGET})

target_include_directories(${TARGET} PRIVATE ${Vulkan_INCLUDE_DIRS})
target_sources(${TARGET} PUBLIC
	VulkanObjects.h
	VulkanObjects.cpp
	main.cpp)

target_link_directories(${TARGET} PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(${TARGET} PRIVATE Threads::Threads)

set_target_properties(
	${TARGET} PROPERTIES
	VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
