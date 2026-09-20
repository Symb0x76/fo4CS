#pragma once

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string_view>

#include "Diagnostics/LogPaths.h"

// Opt-in hang trace (FO4CS_HANG_TRACE=1): appends timestamped stage markers to
// <F4SE log dir>/CommunityShaders.hangtrace.log with an unbuffered write per line,
// so the last marker survives a hard hang.
namespace fo4cs::diagnostics
{
	inline bool IsHangTraceEnabled()
	{
		wchar_t value[8]{};
		const auto length = GetEnvironmentVariableW(L"FO4CS_HANG_TRACE", value, static_cast<DWORD>(std::size(value)));
		return length > 0 && wcscmp(value, L"1") == 0;
	}

	inline std::optional<std::filesystem::path> GetHangTracePath()
	{
		auto path = GetF4SELogDirectory();
		if (!path) {
			return std::nullopt;
		}

		*path /= "CommunityShaders.hangtrace.log";
		return path;
	}

	inline void WriteHangTraceLine(std::string_view a_stage)
	{
		if (!IsHangTraceEnabled()) {
			return;
		}

		auto path = GetHangTracePath();
		if (!path) {
			return;
		}

		auto file = CreateFileW(
			path->c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}

		SYSTEMTIME now{};
		GetLocalTime(&now);

		char line[512]{};
		const auto count = std::snprintf(
			line,
			sizeof(line),
			"%04hu-%02hu-%02hu %02hu:%02hu:%02hu.%03hu tick=%llu tid=%lu %.*s\n",
			now.wYear,
			now.wMonth,
			now.wDay,
			now.wHour,
			now.wMinute,
			now.wSecond,
			now.wMilliseconds,
			static_cast<unsigned long long>(GetTickCount64()),
			static_cast<unsigned long>(GetCurrentThreadId()),
			static_cast<int>(a_stage.size()),
			a_stage.data());
		if (count > 0) {
			DWORD written = 0;
			WriteFile(file, line, static_cast<DWORD>(std::min<int>(count, static_cast<int>(sizeof(line) - 1))), &written, nullptr);
			FlushFileBuffers(file);
		}

		CloseHandle(file);
	}

	inline void ResetHangTrace()
	{
		if (!IsHangTraceEnabled()) {
			return;
		}

		auto path = GetHangTracePath();
		if (path) {
			DeleteFileW(path->c_str());
		}

		WriteHangTraceLine("plugin-load:hangtrace-reset");
	}
}
