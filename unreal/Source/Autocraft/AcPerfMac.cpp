// The Mac's readings for `FAcPerf` (AcPerfSystem.h), the C++ of
// PerfMonitor.swift's `cpuSeconds`, `memory`, `appGPUNanoseconds`,
// `deviceGPUUtilization`, `loadAverage` and `Machine`.
#include "AcPerfSystem.h"

#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <sys/resource.h>
#include <stdlib.h>
#endif

#if PLATFORM_MAC
THIRD_PARTY_INCLUDES_START
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <unistd.h>
THIRD_PARTY_INCLUDES_END
#endif

#if PLATFORM_MAC || PLATFORM_LINUX
double AcPerfSystem::CpuSeconds()
{
	rusage U;
	getrusage(RUSAGE_SELF, &U);
	return U.ru_utime.tv_sec + U.ru_utime.tv_usec / 1e6 + U.ru_stime.tv_sec + U.ru_stime.tv_usec / 1e6;
}

double AcPerfSystem::LoadAverage()
{
	double L[3] = {0, 0, 0};
	return getloadavg(L, 3) > 0 ? L[0] : 0.0;
}
#else
double AcPerfSystem::CpuSeconds() { return 0.0; }
double AcPerfSystem::LoadAverage() { return 0.0; }
#endif

#if PLATFORM_MAC

namespace
{
	/// Visit every GPU (IOAccelerator) registry entry.
	template <typename F>
	void Accelerators(F&& Body)
	{
		io_iterator_t It = 0;
		if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &It) != KERN_SUCCESS) return;
		while (io_registry_entry_t E = IOIteratorNext(It))
		{
			Body(E);
			IOObjectRelease(E);
		}
		IOObjectRelease(It);
	}

	/// The entry's properties (release with CFRelease), or null.
	CFDictionaryRef Properties(io_registry_entry_t E)
	{
		CFMutableDictionaryRef Props = nullptr;
		if (IORegistryEntryCreateCFProperties(E, &Props, kCFAllocatorDefault, 0) != KERN_SUCCESS) return nullptr;
		return Props;
	}

	bool Number(CFTypeRef V, double& Out)
	{
		if (!V || CFGetTypeID(V) != CFNumberGetTypeID()) return false;
		return CFNumberGetValue((CFNumberRef)V, kCFNumberDoubleType, &Out);
	}

	bool Number64(CFTypeRef V, uint64& Out)
	{
		if (!V || CFGetTypeID(V) != CFNumberGetTypeID()) return false;
		int64 X = 0;
		if (!CFNumberGetValue((CFNumberRef)V, kCFNumberSInt64Type, &X)) return false;
		Out = (uint64)X;
		return true;
	}

	FString Sysctl(const char* Name)
	{
		size_t Size = 0;
		if (sysctlbyname(Name, nullptr, &Size, nullptr, 0) != 0 || Size == 0) return FString();
		TArray<char> Buf;
		Buf.SetNumZeroed((int32)Size + 1);
		sysctlbyname(Name, Buf.GetData(), &Size, nullptr, 0);
		return FString(UTF8_TO_TCHAR(Buf.GetData()));
	}

	int32 SysctlInt(const char* Name)
	{
		int32 V = 0;
		size_t Size = sizeof(V);
		return sysctlbyname(Name, &V, &Size, nullptr, 0) == 0 ? V : 0;
	}
}

void AcPerfSystem::Memory(double& Footprint, double& Graphics, double& Peak)
{
	task_vm_info_data_t Info;
	mach_msg_type_number_t Count = TASK_VM_INFO_COUNT;
	if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&Info, &Count) != KERN_SUCCESS)
	{
		const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
		Footprint = Stats.UsedPhysical / 1048576.0;
		Peak = Stats.PeakUsedPhysical / 1048576.0;
		Graphics = 0;
		return;
	}
	constexpr double MB = 1048576.0;
	Footprint = Info.phys_footprint / MB;
	Graphics = (Info.ledger_tag_graphics_footprint + Info.ledger_tag_graphics_footprint_compressed) / MB;
	Peak = Info.ledger_phys_footprint_peak / MB;
}

double AcPerfSystem::AppGpuNanoseconds()
{
	// The accelerator's user clients whose creator is this process.
	const FString TagText = FString::Printf(TEXT("pid %d,"), (int32)getpid());
	const FTCHARToUTF8 Tag(*TagText);
	uint64 Total = 0;
	bool bFound = false;
	Accelerators([&](io_registry_entry_t Acc)
	{
		io_iterator_t It = 0;
		if (IORegistryEntryGetChildIterator(Acc, kIOServicePlane, &It) != KERN_SUCCESS) return;
		while (io_registry_entry_t C = IOIteratorNext(It))
		{
			if (CFDictionaryRef P = Properties(C))
			{
				CFTypeRef Creator = CFDictionaryGetValue(P, CFSTR("IOUserClientCreator"));
				char Buf[256] = {0};
				if (Creator && CFGetTypeID(Creator) == CFStringGetTypeID()
					&& CFStringGetCString((CFStringRef)Creator, Buf, sizeof(Buf), kCFStringEncodingUTF8)
					&& FCStringAnsi::Strncmp(Buf, Tag.Get(), Tag.Length()) == 0)
				{
					CFTypeRef Usage = CFDictionaryGetValue(P, CFSTR("AppUsage"));
					if (Usage && CFGetTypeID(Usage) == CFArrayGetTypeID())
					{
						bFound = true;
						const CFIndex N = CFArrayGetCount((CFArrayRef)Usage);
						for (CFIndex I = 0; I < N; ++I)
						{
							CFTypeRef U = CFArrayGetValueAtIndex((CFArrayRef)Usage, I);
							if (!U || CFGetTypeID(U) != CFDictionaryGetTypeID()) continue;
							uint64 Ns = 0;
							if (Number64(CFDictionaryGetValue((CFDictionaryRef)U, CFSTR("accumulatedGPUTime")), Ns)) Total += Ns;
						}
					}
				}
				CFRelease(P);
			}
			IOObjectRelease(C);
		}
		IOObjectRelease(It);
	});
	return bFound ? (double)Total : -1.0;
}

double AcPerfSystem::ChipGpuUtilization()
{
	double Result = -1;
	Accelerators([&](io_registry_entry_t E)
	{
		CFTypeRef Perf = IORegistryEntryCreateCFProperty(E, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
		if (!Perf) return;
		if (CFGetTypeID(Perf) == CFDictionaryGetTypeID())
		{
			double U = 0;
			if (Number(CFDictionaryGetValue((CFDictionaryRef)Perf, CFSTR("Device Utilization %")), U)) Result = U;
		}
		CFRelease(Perf);
	});
	return Result;
}

FString AcPerfSystem::Machine()
{
	int32 GpuCores = 0;
	Accelerators([&](io_registry_entry_t E)
	{
		CFTypeRef N = IORegistryEntryCreateCFProperty(E, CFSTR("gpu-core-count"), kCFAllocatorDefault, 0);
		double V = 0;
		if (N && Number(N, V)) GpuCores = (int32)V;
		if (N) CFRelease(N);
	});
	const FPlatformMemoryConstants& Mem = FPlatformMemory::GetConstants();
	return FString::Printf(TEXT("%s (%s), GPU %s cores, CPU %dP+%dE, %.0f GB, macOS %s"), *Sysctl("machdep.cpu.brand_string"),
		*Sysctl("hw.model"), GpuCores > 0 ? *FString::FromInt(GpuCores) : TEXT("?"), SysctlInt("hw.perflevel0.physicalcpu"),
		SysctlInt("hw.perflevel1.physicalcpu"), Mem.TotalPhysical / 1073741824.0, *Sysctl("kern.osproductversion"));
}

#else

void AcPerfSystem::Memory(double& Footprint, double& Graphics, double& Peak)
{
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	Footprint = Stats.UsedPhysical / 1048576.0;
	Peak = Stats.PeakUsedPhysical / 1048576.0;
	Graphics = 0;
}

double AcPerfSystem::AppGpuNanoseconds() { return -1.0; }
double AcPerfSystem::ChipGpuUtilization() { return -1.0; }

FString AcPerfSystem::Machine()
{
	return FString::Printf(TEXT("%s, %s"), *FPlatformMisc::GetCPUBrand(), *FPlatformMisc::GetOSVersion());
}

#endif
