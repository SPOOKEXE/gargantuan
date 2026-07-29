cmake_minimum_required(VERSION 3.20)
project(gargantuan CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Deliberately no default build type. An unoptimised build is the one that shows
# where the code is actually doing too much work: -O0 leaves every inefficiency
# on the profile where it can be seen and fixed, rather than letting the
# inliner hide it. Ship with `-DCMAKE_BUILD_TYPE=Release` when that is what is
# wanted; measured on examples/SkyGrid.luau the difference is roughly tenfold on
# the opaque pass, so the two are not comparable and are not meant to be.

# Shaders

find_program(GLSL_COMPILER glslc REQUIRED)
if(APPLE)
	find_program(SPIRV_CROSS spirv-cross)
	find_program(XCRUN xcrun)

	if(NOT SPIRV_CROSS)
		message(WARNING "Missing spirv-cross, install via `brew install spirv-cross` to compile Metal shaders.")
	endif()
endif()

file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS
	"assets/shaders/*.vert"
	"assets/shaders/*.frag"
	"assets/shaders/*.comp"
)

set(SHADER_OUTPUTS "")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
foreach(SHADER IN LISTS SHADER_SOURCES)
	get_filename_component(SHADER_NAME ${SHADER} NAME)

	set(SPIRV_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
	add_custom_command(
		OUTPUT ${SPIRV_OUTPUT}
		COMMAND ${GLSL_COMPILER} ${SHADER} -o ${SPIRV_OUTPUT}
		DEPENDS ${SHADER}
		COMMENT "Compiling SPIR-V ${SHADER_NAME}"
	)
	list(APPEND SHADER_OUTPUTS ${SPIRV_OUTPUT})

	if(APPLE AND SPIRV_CROSS AND XCRUN)
		set(METAL_SRC "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.metal")
		set(METAL_AIR "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.air")
		set(METALLIB_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.metallib")

		# SPIR-V -> MSL
		add_custom_command(
				OUTPUT ${METAL_SRC}
				COMMAND ${SPIRV_CROSS} ${SPIRV_OUTPUT} --msl --output ${METAL_SRC}
				DEPENDS ${SPIRV_OUTPUT}
				COMMENT "Cross-compiling SPIR-V to MSL: ${SHADER_NAME}"
			)

		# MSL -> AIR (.air) -> Metal Library
		add_custom_command(
				OUTPUT ${METALLIB_OUTPUT}
				COMMAND ${XCRUN} -sdk macosx metal -c ${METAL_SRC} -o ${METAL_AIR}
				COMMAND ${XCRUN} -sdk macosx metallib ${METAL_AIR} -o ${METALLIB_OUTPUT}
				DEPENDS ${METAL_SRC}
				COMMENT "Building Metal Library: ${SHADER_NAME}.metallib"
			)

		list(APPEND SHADER_OUTPUTS ${METALLIB_OUTPUT})
	endif()
endforeach()

set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_RENDER ON CACHE BOOL "" FORCE)
set(SDL_DISABLE_PCH ON CACHE BOOL "" FORCE)
set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)

# Optional: compile GLSL inside the process rather than shelling out to glslc.
# Without it, runtime shader code needs glslc on PATH; build-time shaders are
# unaffected either way.
#
# The Vulkan SDK ships shaderc but puts it nowhere the default search paths
# look, which is how a Windows install with everything present still ends up
# shelling out to glslc.
set(GARGANTUAN_SHADERC_HINTS "")
if(DEFINED ENV{VULKAN_SDK})
	file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" GARGANTUAN_VULKAN_SDK)
	list(APPEND GARGANTUAN_SHADERC_HINTS "${GARGANTUAN_VULKAN_SDK}")
endif()

find_path(SHADERC_INCLUDE_DIR shaderc/shaderc.h
	HINTS ${GARGANTUAN_SHADERC_HINTS}
	PATH_SUFFIXES include Include
)
find_library(SHADERC_LIBRARY NAMES shaderc_shared shaderc
	HINTS ${GARGANTUAN_SHADERC_HINTS}
	PATH_SUFFIXES lib Lib
)
if(WIN32)
	find_file(SHADERC_DLL NAMES shaderc_shared.dll
		HINTS ${GARGANTUAN_SHADERC_HINTS}
		PATH_SUFFIXES bin Bin
	)
endif()
if(SHADERC_INCLUDE_DIR AND SHADERC_LIBRARY)
	message(STATUS "Found shaderc, runtime shaders will compile in-process")
	set(GARGANTUAN_SHADERC TRUE)
else()
	message(STATUS "No shaderc, runtime shaders will shell out to glslc")
	set(GARGANTUAN_SHADERC FALSE)
endif()

# A clone without --recursive leaves the vendor folders empty, and the
# add_subdirectory calls below then fail with "source directory does not contain
# a CMakeLists.txt file", which says nothing about the actual problem.
set(GARGANTUAN_VENDORED box3d glm json luau magicenum sdl spdlog tracy)

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
	option(GARGANTUAN_CHECKOUT_VENDORED "Check vendored dependencies out automatically" ON)
	if(GARGANTUAN_CHECKOUT_VENDORED)
		message(STATUS "Checking out vendored dependencies")
		execute_process(
			COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			RESULT_VARIABLE GARGANTUAN_SUBMODULE_RESULT
		)
		if(NOT GARGANTUAN_SUBMODULE_RESULT EQUAL 0)
			message(FATAL_ERROR
				"git submodule update --init --recursive failed with ${GARGANTUAN_SUBMODULE_RESULT}. "
				"Run it by hand, or configure with -DGARGANTUAN_CHECKOUT_VENDORED=OFF if the "
				"vendor folders are being filled some other way.")
		endif()
	endif()
endif()

# Reachable from a source tarball, where there is no .git to ask
foreach(GARGANTUAN_DEPENDENCY IN LISTS GARGANTUAN_VENDORED)
	file(GLOB GARGANTUAN_DEPENDENCY_FILES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/${GARGANTUAN_DEPENDENCY}/*")
	if(NOT GARGANTUAN_DEPENDENCY_FILES)
		message(FATAL_ERROR
			"vendor/${GARGANTUAN_DEPENDENCY} is empty. It is a git submodule: run "
			"`git submodule update --init --recursive` in ${CMAKE_CURRENT_SOURCE_DIR}.")
	endif()
endforeach()

add_subdirectory(vendor/box3d)
add_subdirectory(vendor/glm)
add_subdirectory(vendor/json)
add_subdirectory(vendor/luau)
add_subdirectory(vendor/sdl)
add_subdirectory(vendor/spdlog)

# Tracing.
#
# The client is always compiled in. It collects nothing until Tracy's profiler
# connects, which is what TRACY_ON_DEMAND buys: a build nobody is watching pays
# an atomic read per zone and holds no buffers. Turn GARGANTUAN_TRACY off and
# every G_PROFILE compiles to nothing at all.
option(GARGANTUAN_TRACY "Compile in the Tracy client" ON)
set(TRACY_ENABLE ${GARGANTUAN_TRACY} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
# Traces leave the process over a socket. Refusing anything off this machine is
# the right default for a profiler that is on in ordinary builds.
set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
add_subdirectory(vendor/tracy)

# The viewer and the headless capture tool. Off by default because they are
# their own programs with their own dependencies, and configuring them reaches
# the network for whatever the system does not already have.
#
# ExternalProject rather than add_subdirectory: each is its own CMake project
# and the two of them declare targets of the same names (TracyServer,
# TracyCapstone, libzstd), so they cannot share a build tree with us or with
# each other. Separate trees also keep their dependency downloads out of ours.
#
# They land in build/bin, which is where their own install rules put them.
option(GARGANTUAN_TRACY_TOOLS "Build tracy-profiler and tracy-capture" OFF)
if(GARGANTUAN_TRACY_TOOLS)
	include(ExternalProject)

	# The viewer picks its window backend at build time and defaults to
	# Wayland. Nothing detects this at runtime, so an X11 session gets a binary
	# that cannot open a window unless it is told now.
	if(NOT WIN32 AND NOT APPLE AND NOT DEFINED ENV{WAYLAND_DISPLAY})
		set(GARGANTUAN_TRACY_LEGACY ON)
	else()
		set(GARGANTUAN_TRACY_LEGACY OFF)
	endif()

	ExternalProject_Add(tracy-capture
		SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tracy/capture"
		BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/tracy/capture"
		CMAKE_ARGS
			-DCMAKE_BUILD_TYPE=Release
			-DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}
	)

	ExternalProject_Add(tracy-profiler
		SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tracy/profiler"
		BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/tracy/profiler"
		CMAKE_ARGS
			-DCMAKE_BUILD_TYPE=Release
			-DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}
			-DLEGACY=${GARGANTUAN_TRACY_LEGACY}
	)

	# Turns a .tracy into per-zone totals on stdout, which is how two runs get
	# compared without a window open. This is what the old markdown report was
	# for, and it is Tracy's own tool rather than ours.
	ExternalProject_Add(tracy-csvexport
		SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tracy/csvexport"
		BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/tracy/csvexport"
		CMAKE_ARGS
			-DCMAKE_BUILD_TYPE=Release
			-DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}
	)
endif()

file(GLOB_RECURSE GARGANTUAN_SOURCES CONFIGURE_DEPENDS
	"src/*.cpp"
	"src/*.hpp"
)

add_executable(gargantuan ${GARGANTUAN_SOURCES} ${SHADER_OUTPUTS})

# Members named after their own type (RenderProvider *RenderProvider) keep the
# Luau-facing API familiar, but GCC 13+ rejects them by default. Clang does not
# implement this diagnostic, which is why it only bites on GCC.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(gargantuan PRIVATE -Wno-changes-meaning)
endif()

target_include_directories(gargantuan PRIVATE
	"src"
	"include"
)

target_include_directories(gargantuan SYSTEM PRIVATE
	"vendor/box3d/include"
	"vendor/glm/glm"
	"vendor/luau/Common/include"
	"vendor/luau/Compiler/include"
	"vendor/luau/Ast/include"
	"vendor/luau/VM/include"
	"vendor/luau/Require/include"
	"vendor/luau/Config/include"
	"vendor/luau/CodeGen/include"
	"vendor/magicenum/include"
	"vendor/sdl/include"
	"vendor/spdlog/include"
	"vendor/stb"
)

target_link_libraries(gargantuan
	PRIVATE
		SDL3::SDL3
		box3d
		glm
		nlohmann_json
		spdlog::spdlog
		Luau.Compiler
		Luau.CodeGen
		Luau.Ast
		Luau.VM
		Luau.Require
		Luau.Config
		Tracy::TracyClient
)

if(GARGANTUAN_SHADERC)
	target_compile_definitions(gargantuan PRIVATE GARGANTUAN_HAVE_SHADERC)
	target_include_directories(gargantuan SYSTEM PRIVATE ${SHADERC_INCLUDE_DIR})
	target_link_libraries(gargantuan PRIVATE ${SHADERC_LIBRARY})
endif()

if(WIN32)
	add_custom_command(TARGET gargantuan POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:SDL3::SDL3>"
			"$<TARGET_FILE_DIR:gargantuan>"
		COMMENT "Copying SDL3.dll next to gargantuan.exe"
	)

	# shaderc is linked as a shared library, so Windows needs its DLL beside the
	# executable the same way SDL3 does
	if(GARGANTUAN_SHADERC AND SHADERC_DLL)
		add_custom_command(TARGET gargantuan POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${SHADERC_DLL}"
				"$<TARGET_FILE_DIR:gargantuan>"
			COMMENT "Copying shaderc_shared.dll next to gargantuan.exe"
		)
	endif()
endif()
