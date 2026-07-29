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
find_path(SHADERC_INCLUDE_DIR shaderc/shaderc.h)
find_library(SHADERC_LIBRARY NAMES shaderc_shared shaderc)
if(WIN32)
	find_file(SHADERC_DLL NAMES shaderc_shared.dll)
endif()
if(SHADERC_INCLUDE_DIR AND SHADERC_LIBRARY)
	message(STATUS "Found shaderc, runtime shaders will compile in-process")
	set(GARGANTUAN_SHADERC TRUE)
else()
	message(STATUS "No shaderc, runtime shaders will shell out to glslc")
	set(GARGANTUAN_SHADERC FALSE)
endif()

# Every dependency is a git submodule, and a clone without --recursive leaves
# the vendor folders empty. Left alone, the add_subdirectory calls below then
# fail with "source directory does not contain a CMakeLists.txt file", which
# says nothing about the actual problem.
set(GARGANTUAN_VENDORED box3d glm json luau magicenum sdl spdlog)

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

# Says which one is missing rather than which folder has no CMakeLists in it.
# Reachable from a source tarball, where there is no .git to ask.
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
