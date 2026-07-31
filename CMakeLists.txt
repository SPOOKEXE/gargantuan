cmake_minimum_required(VERSION 3.20)
project(gargantuan CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The binary, shaders, and shared runtime libraries must share an output tree.
if(ANDROID)
	# Toolchains expose the ABI under either name.
	if(NOT ANDROID_ABI)
		set(ANDROID_ABI "${CMAKE_ANDROID_ARCH_ABI}")
	endif()

	# Gradle consumes shared libraries from lib/<abi>/ and data from assets/.
	set(GARGANTUAN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI}")
else()
	set(GARGANTUAN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${GARGANTUAN_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${GARGANTUAN_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

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

# Multi-config generators do not expose the final binary directory at configure time.
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

		add_custom_command(
				OUTPUT ${METAL_SRC}
				COMMAND ${SPIRV_CROSS} ${SPIRV_OUTPUT} --msl --output ${METAL_SRC}
				DEPENDS ${SPIRV_OUTPUT}
				COMMENT "Cross-compiling SPIR-V to MSL: ${SHADER_NAME}"
			)

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
set(FLECS_STATIC ON CACHE BOOL "" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_TESTS OFF CACHE BOOL "" FORCE)

# box3d has unconditional install rules; exclusion keeps its artifacts out of distributions.
add_subdirectory(vendor/box3d EXCLUDE_FROM_ALL)
add_subdirectory(vendor/flecs)
add_subdirectory(vendor/glm)
add_subdirectory(vendor/json)
add_subdirectory(vendor/luau)
add_subdirectory(vendor/sdl)
add_subdirectory(vendor/spdlog)

option(GARGANTUAN_TRACY "Compile in the Tracy client" ON)
set(TRACY_ENABLE ${GARGANTUAN_TRACY} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
# Profiling is enabled in ordinary builds, so refuse remote connections by default.
set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
# Tracy also has unconditional install rules.
add_subdirectory(vendor/tracy EXCLUDE_FROM_ALL)

# These tools may fetch dependencies and have colliding target names, so keep separate build trees.
option(GARGANTUAN_TRACY_TOOLS "Build tracy-profiler, tracy-capture and tracy-csvexport" OFF)
if(GARGANTUAN_TRACY_TOOLS)
	include(ExternalProject)

	# Tracy chooses its Linux window backend at build time.
	if(NOT WIN32 AND NOT APPLE AND NOT DEFINED ENV{WAYLAND_DISPLAY})
		set(GARGANTUAN_TRACY_LEGACY ON)
	else()
		set(GARGANTUAN_TRACY_LEGACY OFF)
	endif()

	foreach(GARGANTUAN_TRACY_TOOL capture csvexport profiler)
		ExternalProject_Add(tracy-${GARGANTUAN_TRACY_TOOL}
			SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tracy/${GARGANTUAN_TRACY_TOOL}"
			BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/tracy/${GARGANTUAN_TRACY_TOOL}"
			CMAKE_ARGS
				-DCMAKE_BUILD_TYPE=Release
				-DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}
				-DLEGACY=${GARGANTUAN_TRACY_LEGACY}
		)
	endforeach()
endif()

# shaderc is optional; precompiled shaders remain available without it.
find_path(SHADERC_INCLUDE_DIR shaderc/shaderc.h PATH_SUFFIXES include Include)
find_library(SHADERC_LIBRARY NAMES shaderc_shared shaderc PATH_SUFFIXES lib Lib)

file(GLOB_RECURSE GARGANTUAN_SOURCES CONFIGURE_DEPENDS
	"src/*.cpp"
	"src/*.hpp"
)

if(ANDROID)
	# The Java activity loads libmain and resolves SDL_main from it.
	add_library(gargantuan SHARED ${GARGANTUAN_SOURCES} ${SHADER_OUTPUTS})
	set_target_properties(gargantuan PROPERTIES OUTPUT_NAME "main")
else()
	add_executable(gargantuan ${GARGANTUAN_SOURCES} ${SHADER_OUTPUTS})
endif()

# Roblox-compatible names intentionally reuse type names; GCC 13+ rejects this by default.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(gargantuan PRIVATE -Wno-changes-meaning)
endif()

target_include_directories(gargantuan PRIVATE
	"src"
	"include"
)

if(SHADERC_INCLUDE_DIR AND SHADERC_LIBRARY)
	message(STATUS "shaderc found: runtime shader compilation enabled")
	target_include_directories(gargantuan SYSTEM PRIVATE ${SHADERC_INCLUDE_DIR})
	target_link_libraries(gargantuan PRIVATE ${SHADERC_LIBRARY})
	target_compile_definitions(gargantuan PRIVATE GARGANTUAN_HAVE_SHADERC)
else()
	message(STATUS "shaderc not found: runtime shader compilation disabled")
endif()

target_include_directories(gargantuan SYSTEM PRIVATE
	"vendor/box3d/include"
	"vendor/flecs/include"
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
		flecs::flecs_static
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

# Shader lookup is relative to the runtime base path; Android packages them as assets.
if(ANDROID)
	# Android shader reads must use SDL_IOFromFile; SDL_GetBasePath() is NULL for APK assets.
	set(GARGANTUAN_DATA_DIR "${CMAKE_BINARY_DIR}/assets")
else()
	set(GARGANTUAN_DATA_DIR "$<TARGET_FILE_DIR:gargantuan>")
endif()

# Keep this independent of relinking so shader-only edits are always staged.
add_custom_target(gargantuan_shaders ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${GARGANTUAN_SHADER_STAGE}"
		"${GARGANTUAN_DATA_DIR}/shaders"
	DEPENDS ${SHADER_OUTPUTS}
	COMMENT "Staging shaders into ${GARGANTUAN_DATA_DIR}/shaders"
	VERBATIM
)
add_dependencies(gargantuan_shaders gargantuan)

# Unix loaders must search beside the binary for the shared SDL3 library.
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

if(ANDROID)
	# Gradle is the primary Android packaging path and consumes the build tree directly.
	install(TARGETS gargantuan LIBRARY DESTINATION "lib/${ANDROID_ABI}")
	if(TARGET SDL3-shared)
		install(TARGETS SDL3-shared LIBRARY DESTINATION "lib/${ANDROID_ABI}")
	endif()
	install(DIRECTORY "${GARGANTUAN_SHADER_STAGE}/" DESTINATION "assets/shaders")
elseif(WIN32)
	install(TARGETS gargantuan RUNTIME DESTINATION ".")
	if(TARGET SDL3-shared)
		install(TARGETS SDL3-shared RUNTIME DESTINATION ".")
	endif()
	install(DIRECTORY "${GARGANTUAN_SHADER_STAGE}/" DESTINATION "shaders")
elseif(APPLE)
	# SDL_GetBasePath() resolves to Contents/Resources inside an app bundle.
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
	# Keep .metal for SDL's MSL fallback; .air is build-only.
	install(DIRECTORY "${GARGANTUAN_SHADER_STAGE}/" DESTINATION "${GARGANTUAN_BUNDLE}/Contents/Resources/shaders"
		PATTERN "*.air" EXCLUDE)
else()
	install(TARGETS gargantuan RUNTIME DESTINATION "bin")
	if(TARGET SDL3-shared)
		install(TARGETS SDL3-shared LIBRARY DESTINATION "lib")
	endif()
	install(DIRECTORY "${GARGANTUAN_SHADER_STAGE}/" DESTINATION "bin/shaders")
endif()
