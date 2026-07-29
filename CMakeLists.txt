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

# Build output.
#
# The engine finds its shaders relative to itself: Paths.cpp asks
# SDL_GetBasePath() and Shader.cpp turns that into <base>/shaders/<name>, so
# binary and shaders have to share a directory. Visual Studio and Xcode append
# Debug/ or Release/ to every output directory, which used to put the exe in
# build/Debug/ and the shaders in build/shaders/ -- a Windows build that could
# not load a single shader. Naming the directory here settles it for every
# generator; multi-config still appends the configuration, so Debug and Release
# each get a complete tree instead of overwriting each other.
#
# The vendored dependencies inherit these too, which is what puts SDL3.dll /
# libSDL3.so beside the binary rather than in build/vendor/sdl/.
if(ANDROID)
	# The NDK toolchain sets ANDROID_ABI; CMake's own Android support sets
	# CMAKE_ANDROID_ARCH_ABI instead. Either way we need the name.
	if(NOT ANDROID_ABI)
		set(ANDROID_ABI "${CMAKE_ANDROID_ARCH_ABI}")
	endif()

	# An APK has no bin/. The packager takes shared libraries from lib/<abi>/
	# and everything else from assets/, so building straight into that shape
	# means a Gradle project can point jniLibs.srcDirs and assets.srcDirs at
	# this build tree without an intermediate copy.
	set(GARGANTUAN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI}")
else()
	set(GARGANTUAN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${GARGANTUAN_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${GARGANTUAN_OUTPUT_DIR}")
# Import libraries and the static halves of the vendored dependencies are not
# runtime files and only clutter the directory that is meant to be runnable.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

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

# Staged here rather than written straight next to the binary:
# add_custom_command(OUTPUT) needs a path fixed at configure time, and under a
# multi-config generator the binary's directory is not known until build time.
# A POST_BUILD copy further down bridges the two, and compiling once here means
# every configuration and the Android assets/ tree share the result.
set(GARGANTUAN_SHADER_STAGE "${CMAKE_CURRENT_BINARY_DIR}/shaders")

set(SHADER_OUTPUTS "")
file(MAKE_DIRECTORY "${GARGANTUAN_SHADER_STAGE}")
foreach(SHADER IN LISTS SHADER_SOURCES)
	get_filename_component(SHADER_NAME ${SHADER} NAME)

	set(SPIRV_OUTPUT "${GARGANTUAN_SHADER_STAGE}/${SHADER_NAME}.spv")
	add_custom_command(
		OUTPUT ${SPIRV_OUTPUT}
		COMMAND ${GLSL_COMPILER} ${SHADER} -o ${SPIRV_OUTPUT}
		DEPENDS ${SHADER}
		COMMENT "Compiling SPIR-V ${SHADER_NAME}"
	)
	list(APPEND SHADER_OUTPUTS ${SPIRV_OUTPUT})

	if(APPLE AND SPIRV_CROSS AND XCRUN)
		set(METAL_SRC "${GARGANTUAN_SHADER_STAGE}/${SHADER_NAME}.metal")
		set(METAL_AIR "${GARGANTUAN_SHADER_STAGE}/${SHADER_NAME}.air")
		set(METALLIB_OUTPUT "${GARGANTUAN_SHADER_STAGE}/${SHADER_NAME}.metallib")

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

# EXCLUDE_FROM_ALL because box3d installs libbox3d.a, its headers and a CMake
# package config unconditionally, none of which belongs in a distribution of
# the engine. SDL, glm, json and spdlog gate their install rules on being the
# top-level project and need no such treatment; box3d and tracy do not gate
# theirs. Excluding the directory drops its install rules with it, and the
# library is still built because gargantuan links it.
add_subdirectory(vendor/box3d EXCLUDE_FROM_ALL)
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
# EXCLUDE_FROM_ALL for the same reason as box3d above: tracy's install rules
# are unconditional and would otherwise put libTracyClient.a, the Tracy headers
# and TracyConfig.cmake into the distribution.
add_subdirectory(vendor/tracy EXCLUDE_FROM_ALL)

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

if(ANDROID)
	# Android has no executables in the ordinary sense. The process is started
	# by the Java activity, which does System.loadLibrary("main") and then
	# dlsym()s SDL_main out of it, so the entry point has to be a shared library
	# with exactly that name. SDL3/SDL_main.h in Main.cpp is what renames main
	# to SDL_main and keeps that symbol exported.
	add_library(gargantuan SHARED ${GARGANTUAN_SOURCES} ${SHADER_OUTPUTS})
	set_target_properties(gargantuan PROPERTIES OUTPUT_NAME "main")
else()
	add_executable(gargantuan ${GARGANTUAN_SOURCES} ${SHADER_OUTPUTS})
endif()

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

# Put the compiled shaders where the binary will look for them.
#
# On Android that is the APK's assets/, which is a fixed directory. Everywhere
# else it is whatever directory the binary actually landed in, which is only
# known at build time -- hence the generator expression rather than a path.
if(ANDROID)
	# Note for whoever wires up Gradle: getting the shaders into the APK is only
	# half of it. SDL_GetBasePath() returns NULL on Android because an APK has
	# no base directory, so Paths.cpp falls back to the working directory and
	# Shader.cpp then looks somewhere meaningless. Reading these back out needs
	# SDL_IOFromFile, which knows how to open an asset, rather than the
	# std::filesystem calls used everywhere else.
	set(GARGANTUAN_DATA_DIR "${CMAKE_BINARY_DIR}/assets")
else()
	set(GARGANTUAN_DATA_DIR "$<TARGET_FILE_DIR:gargantuan>")
endif()

add_custom_command(TARGET gargantuan POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${GARGANTUAN_SHADER_STAGE}"
		"${GARGANTUAN_DATA_DIR}/shaders"
	COMMENT "Staging shaders into ${GARGANTUAN_DATA_DIR}/shaders"
	VERBATIM
)

# SDL3 is built shared and lands beside the binary, so the loader has to be
# told to look next to the binary rather than only in the system directories.
# Windows already searches the executable's own directory and needs nothing.
if(APPLE)
	set_target_properties(gargantuan PROPERTIES
		BUILD_RPATH "@executable_path"
		INSTALL_RPATH "@executable_path;@executable_path/../Frameworks"
	)
elseif(UNIX AND NOT ANDROID)
	set_target_properties(gargantuan PROPERTIES
		BUILD_RPATH "$ORIGIN"
		INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib"
	)
endif()

# SDL3.dll needs no copying: CMAKE_RUNTIME_OUTPUT_DIRECTORY above already puts
# it in the same directory as gargantuan.exe. shaderc is a system library found
# with find_file rather than something we build, so it still has to be fetched.
if(WIN32 AND GARGANTUAN_SHADERC AND SHADERC_DLL)
	add_custom_command(TARGET gargantuan POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${SHADERC_DLL}"
			"$<TARGET_FILE_DIR:gargantuan>"
		COMMENT "Copying shaderc_shared.dll next to gargantuan.exe"
	)
endif()

# Installation.
#
# `cmake --install build --prefix dist` produces a tree that can be handed to
# someone without the source. Nothing vendored contributes to it -- see the
# EXCLUDE_FROM_ALL notes above for the two that had to be silenced -- so the
# prefix contains only what is listed below.
#
# The shaders sit beside the binary in every layout rather than somewhere
# tidier like share/. That is not a stylistic choice -- Shader.cpp resolves
# them from SDL_GetBasePath(), and moving them would mean the installed build
# finds nothing.

if(ANDROID)
	# Included for completeness; on Android the real packaging step is Gradle,
	# which reads the build tree directly.
	install(TARGETS gargantuan LIBRARY DESTINATION "lib/${ANDROID_ABI}")
	if(TARGET SDL3-shared)
		install(TARGETS SDL3-shared LIBRARY DESTINATION "lib/${ANDROID_ABI}")
	endif()
	install(DIRECTORY "${GARGANTUAN_SHADER_STAGE}/" DESTINATION "assets/shaders")
elseif(WIN32)
	# Flat, because that is what an unpacked zip is expected to look like.
	install(TARGETS gargantuan RUNTIME DESTINATION ".")
	if(TARGET SDL3-shared)
		install(TARGETS SDL3-shared RUNTIME DESTINATION ".")
	endif()
	if(GARGANTUAN_SHADERC AND SHADERC_DLL)
		install(FILES "${SHADERC_DLL}" DESTINATION ".")
	endif()
	install(DIRECTORY "${GARGANTUAN_SHADER_STAGE}/" DESTINATION "shaders")
elseif(APPLE)
	# SDL_GetBasePath() is NSBundle's resourcePath, so inside a bundle it
	# returns Contents/Resources and outside one it returns the executable's own
	# directory. Both are handled without a line of engine code changing: the
	# build tree stays a loose binary with shaders beside it, and only the
	# installed copy becomes an .app.
	set(GARGANTUAN_BUNDLE "Gargantuan.app")

	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
	<key>CFBundleExecutable</key><string>gargantuan</string>
	<key>CFBundleIdentifier</key><string>com.teamfireworks.gargantuan</string>
	<key>CFBundleName</key><string>Gargantuan</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleShortVersionString</key><string>0.1.0</string>
	<key>CFBundleVersion</key><string>0.1.0</string>
	<key>NSHighResolutionCapable</key><true/>
	<key>LSMinimumSystemVersion</key><string>11.0</string>
</dict>
</plist>
")

	install(TARGETS gargantuan RUNTIME DESTINATION "${GARGANTUAN_BUNDLE}/Contents/MacOS")
	if(TARGET SDL3-shared)
		install(TARGETS SDL3-shared LIBRARY DESTINATION "${GARGANTUAN_BUNDLE}/Contents/Frameworks")
	endif()
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" DESTINATION "${GARGANTUAN_BUNDLE}/Contents")
	# .air files are the intermediate step between the MSL source and the
	# metallib and are of no use at runtime. The .metal sources are kept: they
	# are what SDL falls back to on a driver that reports MSL but not METALLIB.
	install(DIRECTORY "${GARGANTUAN_SHADER_STAGE}/" DESTINATION "${GARGANTUAN_BUNDLE}/Contents/Resources/shaders"
		PATTERN "*.air" EXCLUDE)
else()
	install(TARGETS gargantuan RUNTIME DESTINATION "bin")
	if(TARGET SDL3-shared)
		install(TARGETS SDL3-shared LIBRARY DESTINATION "lib")
	endif()
	install(DIRECTORY "${GARGANTUAN_SHADER_STAGE}/" DESTINATION "bin/shaders")
endif()
