#pragma once

#include "gargantuan/reflection/Enums.hpp"

#include <lua.h>
#include <lualib.h>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace gargantuan {
	// Checked names for build-time assets; Source strings also support unlisted shaders.
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

	// Renderer-owned textures for the camera currently running a shared pass.
	G_ENUM(
		RenderTexture,

		None,

		// Prior finished frame; requesting it retains history and forces redraws.
		History,

		// Unjittered texture-space motion; reproject history at `uv - velocity`.
		Velocity,

		// Linear camera-forward distance in studs; empty pixels use the far plane.
		// Depth and Velocity are produced together on demand.
		Depth,

		// Prior linear depth used to reject occluded or disoccluded history.
		DepthHistory
	)

	// Convenience names only; SPIR-V reflection is authoritative and strings allow runtime names.
	G_ENUM(
		ShaderProperty,

		None,

		// Parameters, in the uniform block
		Background,
		BlockSize,
		Brightness,
		Clamping,
		Contrast,
		Disocclusion,
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

		// Texture bindings, for a ShaderProperties SetImage, SetCameraTexture
		// and SetRenderTexture
		DepthHistoryTexture,
		DepthTexture,
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

	// Returns null for non-enums and errors for an item from the wrong enum type.
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
