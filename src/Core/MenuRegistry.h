#pragma once

#include "Core/IMenuItem.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MenuRegistry
{
	inline std::vector<IMenuItem*>& GetMutableItems()
	{
		static std::vector<IMenuItem*> items;
		return items;
	}

	inline std::mutex& GetMutex()
	{
		static std::mutex mtx;
		return mtx;
	}

	inline void Register(IMenuItem* a_item)
	{
		std::lock_guard lock(GetMutex());
		GetMutableItems().push_back(a_item);
	}

	inline void Unregister(IMenuItem* a_item)
	{
		std::lock_guard lock(GetMutex());
		auto& items = GetMutableItems();
		items.erase(std::remove(items.begin(), items.end(), a_item), items.end());
	}

	inline std::vector<IMenuItem*> GetItems()
	{
		std::lock_guard lock(GetMutex());
		return GetMutableItems();
	}

	inline std::vector<IMenuItem*> GetCategory(const std::string& a_category)
	{
		std::vector<IMenuItem*> result;
		std::lock_guard lock(GetMutex());
		for (auto* item : GetMutableItems()) {
			if (item->GetCategory() == a_category) {
				result.push_back(item);
			}
		}
		return result;
	}

	inline IMenuItem* FindByShortName(const std::string& a_shortName)
	{
		std::lock_guard lock(GetMutex());
		for (auto* item : GetMutableItems()) {
			if (item->GetShortName() == a_shortName) {
				return item;
			}
		}
		return nullptr;
	}
}
