#include "gargantuan/render/ShaderPresets.hpp"

#include <array>
#include <utility>

namespace gargantuan {
	namespace {
		// The enum item is named for the look, the file for what it does, so the
		// two rarely match and every preset gets an entry rather than falling
		// back to a guess that would only be right some of the time
		constexpr std::array<std::pair<Enums::PresetShaders, std::string_view>, 20> PRESET_ASSET_NAMES = {{
			{Enums::PresetShaders::None, ""},
			{Enums::PresetShaders::AnalogueHorror, "analogue_horror"},
			{Enums::PresetShaders::Antialias, "antialias"},
			{Enums::PresetShaders::BlackWhite, "grayscale"},
			{Enums::PresetShaders::BodyCamera, "bodycam"},
			{Enums::PresetShaders::Dither, "dither"},
			{Enums::PresetShaders::EdgeDetect, "edge_detect"},
			{Enums::PresetShaders::FlipHorizontal, "flip_horizontal"},
			{Enums::PresetShaders::FlipVertical, "flip_vertical"},
			{Enums::PresetShaders::Overlay, "overlay"},
			{Enums::PresetShaders::OverlayPair, "overlay2"},
			{Enums::PresetShaders::Pixelate, "pixelate"},
			{Enums::PresetShaders::SecurityCamera, "security_camera"},
			{Enums::PresetShaders::SurfaceTextured, "surface_textured"},
			{Enums::PresetShaders::SurfaceUnlit, "surface_unlit"},
			{Enums::PresetShaders::Swirl, "swirl"},
			{Enums::PresetShaders::TemporalAntialias, "taa"},
			{Enums::PresetShaders::ThermalRed, "thermal"},
			{Enums::PresetShaders::Transpose, "transpose"},
			{Enums::PresetShaders::Vignette, "vignette"},
		}};

		static_assert(
			PRESET_ASSET_NAMES.size() == magic_enum::enum_count<Enums::PresetShaders>(),
			"every PresetShaders item needs an entry in PRESET_ASSET_NAMES"
		);
	}

	std::string_view GetPresetShaderAssetName(Enums::PresetShaders preset) {
		for (const auto &[item, assetName] : PRESET_ASSET_NAMES) {
			if (item == preset) {
				return assetName;
			}
		}
		return {};
	}

	Enums::PresetShaders GetPresetShaderFromAssetName(std::string_view assetName) {
		if (!assetName.empty()) {
			for (const auto &[item, presetAssetName] : PRESET_ASSET_NAMES) {
				if (presetAssetName == assetName) {
					return item;
				}
			}
		}
		return Enums::PresetShaders::None;
	}

	std::string_view GetShaderPropertyName(Enums::ShaderProperty property) {
		if (property == Enums::ShaderProperty::None) {
			return {};
		}
		return magic_enum::enum_name(property);
	}

	std::string CheckShaderPropertyArgument(lua_State *L, int index) {
		if (auto property = TryEnumArgument<Enums::ShaderProperty>(L, index)) {
			return std::string(GetShaderPropertyName(*property));
		}
		return CheckStackValue<std::string>(L, index);
	}

	std::string CheckPresetShaderAssetName(lua_State *L, int index) {
		if (auto preset = TryEnumArgument<Enums::PresetShaders>(L, index)) {
			return std::string(GetPresetShaderAssetName(*preset));
		}
		return CheckStackValue<std::string>(L, index);
	}
}
