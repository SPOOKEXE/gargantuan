#pragma once

#include "gargantuan/reflection/Enums.hpp"

#include <lua.h>
#include <lualib.h>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace gargantuan {
	// The shaders glslc compiles out of assets/shaders at build time, named for
	// the look they produce rather than the file they live in. A script can
	// still set Source to a plain string, which is what a shader outside this
	// list needs, but spelling a built-in one as an enum gets it checked and
	// autocompleted instead of silently missing at render time.
	G_ENUM(
		PresetShaders,

		// No preset; the shader uses its own Code, or a Source string
		None,

		AnalogueHorror,
		Antialias,
		BlackWhite,
		BodyCamera,
		Dither,
		EdgeDetect,
		FlipHorizontal,
		FlipVertical,
		Overlay,
		OverlayPair,
		Pixelate,
		SecurityCamera,
		SurfaceTextured,
		SurfaceUnlit,
		Swirl,
		TemporalAntialias,
		ThermalRed,
		Transpose,
		Vignette
	)

	// Textures the renderer produces for whichever camera is running the pass,
	// rather than anything a script hands it. SetCameraTexture names another
	// camera; this names one of the reader's own buffers, which is the only way
	// a shared pass -- RenderSettings.AntialiasShader runs on every camera --
	// can reach the camera it happens to be running on.
	G_ENUM(
		RenderTexture,

		None,

		// This camera's finished picture from last frame. Asking for it is what
		// makes the engine keep the copy, and a camera whose picture feeds back
		// into itself can never sit still, so it redraws every frame.
		History,

		// Where each pixel was last frame, in texture coordinates: sample the
		// history at `uv - velocity` to find the same surface point again.
		// Written by a geometry pass the camera only pays for when a pass asks
		// for it, and measured off the unjittered projection so the offset a
		// jittering camera adds does not leak into it.
		Velocity
	)

	// Every parameter and texture binding the built-in shaders declare. The
	// engine reads the real list out of a shader's SPIR-V, so this enum is a
	// convenience rather than the authority -- SetNumber and friends still take
	// a string, which is the only way to reach a name a runtime shader invents.
	G_ENUM(
		ShaderProperty,

		None,

		// Parameters, in the uniform block
		Background,
		BlockSize,
		Brightness,
		Clamping,
		Contrast,
		Distortion,
		Feedback,
		FirstPosition,
		Gain,
		Grain,
		Intensity,
		Levels,
		NightVision,
		Noise,
		Opacity,
		Position,
		Radius,
		Scale,
		Scanlines,
		SecondPosition,
		Strength,
		Threshold,
		Tint,
		Tracking,
		Vignette,

		// Texture bindings, for SetImage, SetCameraTexture and SetRenderTexture
		FirstTexture,
		HistoryTexture,
		OutputTexture,
		OverlayTexture,
		SecondTexture,
		Skin,
		SourceTexture,
		SurfaceTexture,
		VelocityTexture
	)

	// The asset name a preset stands for, without extension or directory.
	// Empty for None, and for anything outside the enum.
	std::string_view GetPresetShaderSource(Enums::PresetShaders preset);
	// The preset a Source string names, or None when no preset matches
	Enums::PresetShaders GetPresetShaderFromSource(std::string_view source);

	// A property's name as the shader declares it. Empty for None.
	std::string_view GetShaderPropertyName(Enums::ShaderProperty property);

	// Reads an argument that may be either an enum item of type E or something
	// else entirely. Nothing when the value is not an EnumItem at all, so the
	// caller can fall back to reading it as a string; an error when it is an
	// EnumItem of the wrong enum, which is always a mistake.
	template <typename E>
		requires std::is_enum_v<E>
	std::optional<E> TryEnumArgument(lua_State *L, int index) {
		if (!StackValue<EnumItem>::Is(L, index)) {
			return std::nullopt;
		}

		EnumItem item = StackValue<EnumItem>::From(L, index);
		constexpr std::string_view expected = magic_enum::enum_type_name<E>();
		if (!item.EnumType || item.EnumType->Name != expected) {
			luaL_error(
				L,
				"expected Enum.%.*s, got Enum.%.*s",
				static_cast<int>(expected.size()),
				expected.data(),
				item.EnumType ? static_cast<int>(item.EnumType->Name.size()) : 1,
				item.EnumType ? item.EnumType->Name.data() : "?"
			);
		}

		return static_cast<E>(item.Value);
	}

	// A shader parameter or texture binding, given as either a string or an
	// Enum.ShaderProperty
	std::string CheckShaderPropertyArgument(lua_State *L, int index);
	// A shader asset, given as either a string or an Enum.PresetShaders
	std::string CheckPresetShaderArgument(lua_State *L, int index);
} // namespace gargantuan
