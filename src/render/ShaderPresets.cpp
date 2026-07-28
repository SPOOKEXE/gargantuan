#include "gargantuan/render/ShaderPresets.hpp"

#include <array>
#include <utility>

namespace gargantuan {
	namespace {
		// The enum item is named for the look, the file for what it does, so the
		// two rarely match and every preset gets an entry rather than falling
		// back to a guess that would only be right some of the time
		constexpr std::array<std::pair<Enums::PresetShaders, std::string_view>, 19> PRESET_SOURCES = {{
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
			{Enums::PresetShaders::ThermalRed, "thermal"},
			{Enums::PresetShaders::Transpose, "transpose"},
			{Enums::PresetShaders::Vignette, "vignette"},
		}};

		// Adding an item to the enum and forgetting its asset is a build error
		// rather than a shader that quietly never loads
		static_assert(
			PRESET_SOURCES.size() == magic_enum::enum_count<Enums::PresetShaders>(),
			"every PresetShaders item needs an entry in PRESET_SOURCES"
		);
	} // namespace

	std::string_view GetPresetShaderSource(Enums::PresetShaders preset) {
		for (const auto &[item, source] : PRESET_SOURCES) {
			if (item == preset) {
				return source;
			}
		}
		return {};
	}

	Enums::PresetShaders GetPresetShaderFromSource(std::string_view source) {
		if (!source.empty()) {
			for (const auto &[item, name] : PRESET_SOURCES) {
				if (name == source) {
					return item;
				}
			}
		}
		return Enums::PresetShaders::None;
	}

	std::string_view GetShaderPropertyName(Enums::ShaderProperty property) {
		// Every property is spelled in the shader exactly as it is in the enum,
		// which is the point of writing the enum that way
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

	std::string CheckPresetShaderArgument(lua_State *L, int index) {
		if (auto preset = TryEnumArgument<Enums::PresetShaders>(L, index)) {
			return std::string(GetPresetShaderSource(*preset));
		}
		return CheckStackValue<std::string>(L, index);
	}
} // namespace gargantuan
