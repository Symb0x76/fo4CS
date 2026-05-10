#include "Core/FeatureBuffer.h"
#include "Core/Globals.h"

#include "Features/LightLimitFix.h"

#include <vector>

namespace
{
	template <class... Ts>
	size_t TotalSize(Ts&...)
	{
		return (... + sizeof(Ts));
	}

	template <class... Ts>
	void Serialize(unsigned char* a_dst, Ts&... a_featDatas)
	{
		size_t offset = 0;
		([&] {
			memcpy(a_dst + offset, &a_featDatas, sizeof(a_featDatas));
			offset += sizeof(a_featDatas);
		}(),
			...);
	}
}

std::pair<const unsigned char*, size_t> GetFeatureBufferData(bool /*a_inWorld*/)
{
	static std::vector<unsigned char> buffer;

	auto llfData = globals::features::lightLimitFix.GetCommonBufferData();

	buffer.resize(TotalSize(llfData));
	Serialize(buffer.data(), llfData);

	return { buffer.data(), buffer.size() };
}
