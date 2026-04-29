#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct SearchEntry
{
	std::string label;
	std::string description;
	std::function<void()> focusCallback;
};

struct IMenuItem
{
	virtual ~IMenuItem() = default;

	[[nodiscard]] virtual std::string GetName() = 0;
	[[nodiscard]] virtual std::string GetShortName() = 0;
	[[nodiscard]] virtual std::string_view GetCategory() const { return "Other"; }
	[[nodiscard]] virtual bool IsLoaded() { return true; }
	[[nodiscard]] virtual std::string GetVersion() { return {}; }

	virtual void DrawSettings() {}
	virtual void DrawOverlay() {}
	[[nodiscard]] virtual std::vector<SearchEntry> GetSearchEntries() { return {}; }
	virtual void OnMenuOpened() {}
	virtual void OnMenuClosed() {}
};
