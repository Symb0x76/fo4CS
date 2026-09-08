#include "Upscaling/Upscaler.h"

#include "Core/DebugSwitches.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include "Render/DX12SwapChain.h"
#include "Upscaling/UpscalingInternal.h"

using fo4cs::upscaling::FormatRealFrameRate;
using fo4cs::upscaling::NormalizeRealFrameRateLimit;

void Upscaling::TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
}

void Upscaling::ResolveFrameLimiterPolicy(bool a_startup)
{
	settings.realFrameRateLimit = NormalizeRealFrameRateLimit(settings.realFrameRateLimit, "fRealFrameRateLimit");
	const bool physicsUntiedVerified =
		highFPSPhysicsFixLoaded && highFPSPhysicsFixIniReadable && highFPSPhysicsFixUntiesSpeed;

	FrameLimiterPolicy nextPolicy = FrameLimiterPolicy::kDisabled;
	float nextLimit = 0.0f;
	const char* reason = "user-override";
	if (settings.frameLimitMode) {
		if (physicsUntiedVerified && highFPSPhysicsFixOwnLimiterActive) {
			nextPolicy = FrameLimiterPolicy::kExternal;
			reason = "HighFPSPhysicsFix-own-limiter";
		} else if (physicsUntiedVerified && settings.realFrameRateLimit == 0.0f) {
			nextPolicy = FrameLimiterPolicy::kUnlimited;
			reason = "physics-untied";
		} else if (physicsUntiedVerified) {
			nextPolicy = FrameLimiterPolicy::kLimited;
			nextLimit = settings.realFrameRateLimit;
			reason = "user-real-fps";
		} else {
			nextPolicy = FrameLimiterPolicy::kLimited;
			nextLimit = settings.realFrameRateLimit > 0.0f ? std::min(settings.realFrameRateLimit, 60.0f) : 60.0f;
			reason = "physics-unverified-conservative";
		}
	}

	const bool changed =
		!frameLimiterPolicyInitialized ||
		frameLimiterPolicy != nextPolicy ||
		resolvedRealFrameRateLimit != nextLimit ||
		frameLimiterConfiguredRealFrameRateLimit != settings.realFrameRateLimit;
	if (!a_startup && !changed)
		return;

	frameLimiterPolicy = nextPolicy;
	resolvedRealFrameRateLimit = nextLimit;
	frameLimiterConfiguredRealFrameRateLimit = settings.realFrameRateLimit;
	frameLimiterPolicyInitialized = true;

	const char* resolvedMode = "disabled";
	std::string resolvedValue;
	switch (frameLimiterPolicy) {
	case FrameLimiterPolicy::kExternal:
		resolvedMode = "held";
		break;
	case FrameLimiterPolicy::kUnlimited:
		resolvedMode = "unlimited";
		break;
	case FrameLimiterPolicy::kLimited:
		resolvedValue = FormatRealFrameRate(resolvedRealFrameRateLimit);
		resolvedMode = resolvedValue.c_str();
		break;
	default:
		break;
	}

	logger::info(
		"[FrameGen] limiter policy physicsFixLoaded={} iniReadable={} untie={} externalLimiter={} configuredRealFPS={} resolved={} reason={} inGame={} exterior={} interior={}",
		highFPSPhysicsFixLoaded,
		highFPSPhysicsFixIniReadable,
		highFPSPhysicsFixUntiesSpeed,
		highFPSPhysicsFixOwnLimiterActive,
		FormatRealFrameRate(settings.realFrameRateLimit),
		resolvedMode,
		reason,
		FormatRealFrameRate(highFPSPhysicsFixInGameLimit),
		FormatRealFrameRate(highFPSPhysicsFixExteriorLimit),
		FormatRealFrameRate(highFPSPhysicsFixInteriorLimit));

	if (frameLimiterPolicy == FrameLimiterPolicy::kDisabled && !physicsUntiedVerified) {
		logger::warn("[FrameGen] bFrameLimitMode=false while physics untie is unverified; game speed safety is user-controlled");
	}
}

void Upscaling::FrameLimiter(std::uint32_t a_syncInterval)
{
	ResolveFrameLimiterPolicy(false);

	const bool vsyncHeld = a_syncInterval != 0;
	if (vsyncHeld != frameLimiterVSyncHeld) {
		if (vsyncHeld) {
			logger::info("[FrameGen] limiter held reason=vsync-present syncInterval={}", a_syncInterval);
		} else {
			logger::info("[FrameGen] limiter released reason=vsync-present syncInterval=0");
		}
		frameLimiterVSyncHeld = vsyncHeld;
	}

	static LARGE_INTEGER lastFrame = {};
	LARGE_INTEGER timeNow;
	QueryPerformanceCounter(&timeNow);
	if (vsyncHeld || frameLimiterPolicy != FrameLimiterPolicy::kLimited || resolvedRealFrameRateLimit <= 0.0f) {
		lastFrame = timeNow;
		return;
	}

	LARGE_INTEGER qpf;
	QueryPerformanceFrequency(&qpf);
	const int64_t targetFrameTicks =
		static_cast<int64_t>(static_cast<double>(qpf.QuadPart) / static_cast<double>(resolvedRealFrameRateLimit));
	const int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
	if (lastFrame.QuadPart != 0 && delta < targetFrameTicks) {
		TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
	}
	QueryPerformanceCounter(&lastFrame);
}

/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

double Upscaling::GetRefreshRate(HWND a_window)
{
	HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfoW(monitor, &info) != 0) {
		// using the CCD get the associated path and display configuration
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				// iterate through all the paths until find the exact source to match
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
						// find the matched device which is associated with current device
						// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
						// there may be two callback because source is same target will be different
						// as window is on both the display so either selecting either one is ok
						if (wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
							// get the refresh rate
							UINT numerator = p.targetInfo.refreshRate.Numerator;
							UINT denominator = p.targetInfo.refreshRate.Denominator;
							return (double)numerator / (double)denominator;
						}
					}
				}
			}
		}
	}
	logger::error("Failed to retrieve refresh rate from swap chain");
	return 60;
}
