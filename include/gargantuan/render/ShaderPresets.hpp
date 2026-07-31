#pragma once

#include "gargantuan/reflection/Enums.hpp"

#include <lua.h>
#include <lualib.h>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace gargantuan {
	G_ENUM(
		PresetShaders,

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

	G_ENUM(
		RenderTexture,

		None,

		History,

		Velocity,

		Depth,

		DepthHistory
	)

	G_ENUM(
		ShaderProperty,

		None,

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

	std::string_view GetPresetShaderAssetName(Enums::PresetShaders preset);
	Enums::PresetShaders GetPresetShaderFromAssetName(std::string_view assetName);

	std::string_view GetShaderPropertyName(Enums::ShaderProperty property);

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

	std::string CheckShaderPropertyArgument(lua_State *L, int index);
	std::string CheckPresetShaderAssetName(lua_State *L, int index);
}
