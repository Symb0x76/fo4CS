#pragma once

#include "Core/Feature.h"

class Overlay;

struct FeatureOverlay : Feature
{
	[[nodiscard]] std::string GetName() override { return "Debug Overlay"; }
	[[nodiscard]] std::string GetShortName() override { return "Overlay"; }
	[[nodiscard]] std::string_view GetCategory() const override { return FeatureCategories::kDebug; }
	[[nodiscard]] bool IsCore() const override { return true; }

	Overlay* overlay = nullptr;

	void Load() override;
	void PostPostLoad() override;
	void SetupResources() override;
	void DrawOverlay() override;
	void Reset() override;
};
