// `AcPerfSystem`: the process and machine readings behind `FAcPerf`
// (PerfMonitor.swift's "System readings"), per platform. The Mac's are in
// AcPerfMac.cpp (task_info, IOKit's per-process GPU accounting); elsewhere
// they fall back to Unreal's own numbers or "unknown" (< 0).
#pragma once

#include "CoreMinimal.h"

namespace AcPerfSystem
{
	/// CPU time this process has used, seconds (user + system).
	double CpuSeconds();
	/// Memory footprint (what Activity Monitor shows), its graphics part and
	/// the footprint's peak, MB.
	void Memory(double& Footprint, double& Graphics, double& Peak);
	/// GPU time this process has used, ns, from the driver's per-client
	/// accounting; < 0 when unknown.
	double AppGpuNanoseconds();
	/// The whole chip's GPU utilisation (0…100); < 0 when unknown.
	double ChipGpuUtilization();
	/// The 1-minute load average (runnable threads, all processes).
	double LoadAverage();
	/// "Apple M4 Max (Mac16,5), GPU 40 cores, CPU 12P+4E, 128 GB, macOS 26.3".
	FString Machine();
}
