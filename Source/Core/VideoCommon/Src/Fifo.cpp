// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "VideoConfig.h"
#include "MemoryUtil.h"
#include "Thread.h"
#include "Atomic.h"
#include "OpcodeDecoding.h"
#include "CommandProcessor.h"
#include "PixelEngine.h"
#include "ChunkFile.h"
#include "Fifo.h"
#include "HW/Memmap.h"
#include "Core.h"
#include "CoreTiming.h"

volatile bool g_bSkipCurrentFrame = false;
extern u8* g_pVideoData;

namespace
{
static volatile bool GpuRunningState = false;
static volatile bool EmuRunningState = false;
static std::mutex m_csHWVidOccupied;
// STATE_TO_SAVE
static u8 *videoBuffer;
static int size = 0;
}  // namespace

void Fifo_DoState(PointerWrap &p) 
{
	p.DoArray(videoBuffer, FIFO_SIZE);
	p.Do(size);
	p.DoPointer(g_pVideoData, videoBuffer);
	p.Do(g_bSkipCurrentFrame);
}

void Fifo_PauseAndLock(bool doLock, bool unpauseOnUnlock)
{
	if (doLock)
	{
		EmulatorState(false);
		if (!Core::IsGPUThread())
			m_csHWVidOccupied.lock();
		_dbg_assert_(COMMON, !CommandProcessor::fifo.isGpuReadingData);
	}
	else
	{
		if (unpauseOnUnlock)
			EmulatorState(true);
		if (!Core::IsGPUThread())
			m_csHWVidOccupied.unlock();
	}
}


void Fifo_Init()
{
	videoBuffer = (u8*)AllocateMemoryPages(FIFO_SIZE);
	size = 0;
	GpuRunningState = false;
	Common::AtomicStore(CommandProcessor::VITicks, CommandProcessor::m_cpClockOrigin);
}

void Fifo_Shutdown()
{
	if (GpuRunningState) PanicAlert("Fifo shutting down while active");
	FreeMemoryPages(videoBuffer, FIFO_SIZE);
}

u8* GetVideoBufferStartPtr()
{
	return videoBuffer;
}

u8* GetVideoBufferEndPtr()
{
	return &videoBuffer[size];
}

void Fifo_SetRendering(bool enabled)
{
	g_bSkipCurrentFrame = !enabled;
}

// May be executed from any thread, even the graphics thread.
// Created to allow for self shutdown.
void ExitGpuLoop()
{
	// This should break the wait loop in CPU thread
	CommandProcessor::fifo.bFF_GPReadEnable = false;
	SCPFifoStruct &fifo = CommandProcessor::fifo;
	while(fifo.isGpuReadingData) Common::YieldCPU();
	// Terminate GPU thread loop
	GpuRunningState = false;
	EmuRunningState = true;
}

void EmulatorState(bool running)
{
	EmuRunningState = running;
}


// Description: RunGpuLoop() sends data through this function.
void ReadDataFromFifo(u8* _uData, u32 len)
{
	if (size + len >= FIFO_SIZE)
	{
		int pos = (int)(g_pVideoData - videoBuffer);
		size -= pos;
		if (size + len > FIFO_SIZE)
		{
			PanicAlert("FIFO out of bounds (size = %i, len = %i at %08x)", size, len, pos);
		}
		memmove(&videoBuffer[0], &videoBuffer[pos], size);
		g_pVideoData = videoBuffer;
	}
	// Copy new video instructions to videoBuffer for future use in rendering the new picture
	memcpy(videoBuffer + size, _uData, len);
	size += len;
}

void ResetVideoBuffer()
{
	g_pVideoData = videoBuffer;
	size = 0;
}

LARGE_INTEGER LastSwap;
LARGE_INTEGER Freq;
// Description: Main FIFO update loop
// Purpose: Keep the Core HW updated about the CPU-GPU distance
void RunGpuLoop()
{
	std::lock_guard<std::mutex> lk(m_csHWVidOccupied);
	GpuRunningState = true;
	SCPFifoStruct &fifo = CommandProcessor::fifo;
	u32 cyclesExecuted = 0;

	QueryPerformanceFrequency(&Freq);
	QueryPerformanceCounter(&LastSwap);

	LARGE_INTEGER max_oculus_time;
	max_oculus_time.QuadPart = (Freq.QuadPart / 75LL);

	while (GpuRunningState)
	{
		g_video_backend->PeekMessages();

		VideoFifo_CheckAsyncRequest();

		LARGE_INTEGER CurTime;
		QueryPerformanceCounter(&CurTime);
		
		if ((CurTime.QuadPart - LastSwap.QuadPart) > max_oculus_time.QuadPart)
		{
			wchar_t tmp[256];
			//swprintf(tmp,L"Forcing Swap %d vs %d\n", (int)(CurTime.QuadPart - LastSwap.QuadPart), (int)max_oculus_time.QuadPart);
			OutputDebugString(tmp);
			//Force a lightweight swap
			VideoFifo_DoLightSwap();
			LastSwap = CurTime;
		}
		
		CommandProcessor::SetCpStatus();

		Common::AtomicStore(CommandProcessor::VITicks, CommandProcessor::m_cpClockOrigin);

		// check if we are able to run this buffer	
		while (GpuRunningState && !CommandProcessor::interruptWaiting && fifo.bFF_GPReadEnable && fifo.CPReadWriteDistance && !AtBreakpoint())
		{
			fifo.isGpuReadingData = true;
			CommandProcessor::isPossibleWaitingSetDrawDone = fifo.bFF_GPLinkEnable ? true : false;

			if (!Core::g_CoreStartupParameter.bSyncGPU || Common::AtomicLoad(CommandProcessor::VITicks) > CommandProcessor::m_cpClockOrigin)
			{
				u32 readPtr = fifo.CPReadPointer;
				u8 *uData = Memory::GetPointer(readPtr);

				if (readPtr == fifo.CPEnd)
					readPtr = fifo.CPBase;
				else
					readPtr += 32;

				_assert_msg_(COMMANDPROCESSOR, (s32)fifo.CPReadWriteDistance - 32 >= 0 ,
					"Negative fifo.CPReadWriteDistance = %i in FIFO Loop !\nThat can produce instability in the game. Please report it.", fifo.CPReadWriteDistance - 32);

				ReadDataFromFifo(uData, 32);

				cyclesExecuted = OpcodeDecoder_Run(g_bSkipCurrentFrame);

				if (Core::g_CoreStartupParameter.bSyncGPU && Common::AtomicLoad(CommandProcessor::VITicks) > cyclesExecuted)
					Common::AtomicAdd(CommandProcessor::VITicks, -(s32)cyclesExecuted);

				Common::AtomicStore(fifo.CPReadPointer, readPtr);
				Common::AtomicAdd(fifo.CPReadWriteDistance, -32);
				if((GetVideoBufferEndPtr() - g_pVideoData) == 0)
					Common::AtomicStore(fifo.SafeCPReadPointer, fifo.CPReadPointer);
			}

			CommandProcessor::SetCpStatus();
		
			// This call is pretty important in DualCore mode and must be called in the FIFO Loop.
			// If we don't, s_swapRequested or s_efbAccessRequested won't be set to false
			// leading the CPU thread to wait in Video_BeginField or Video_AccessEFB thus slowing things down.
			VideoFifo_CheckAsyncRequest();		
			CommandProcessor::isPossibleWaitingSetDrawDone = false;
		}

		fifo.isGpuReadingData = false;

		if (EmuRunningState)
		{
			// NOTE(jsd): Calling SwitchToThread() on Windows 7 x64 is a hot spot, according to profiler.
			// See https://docs.google.com/spreadsheet/ccc?key=0Ah4nh0yGtjrgdFpDeF9pS3V6RUotRVE3S3J4TGM1NlE#gid=0
			// for benchmark details.
#if 0
			Common::YieldCPU();
#endif
		}
		else
		{
			// While the emu is paused, we still handle async requests then sleep.
			while (!EmuRunningState)
			{
				g_video_backend->PeekMessages();
				m_csHWVidOccupied.unlock();
				Common::SleepCurrentThread(1);
				m_csHWVidOccupied.lock();
			}
		}
	}
}


bool AtBreakpoint()
{
	SCPFifoStruct &fifo = CommandProcessor::fifo;
	return fifo.bFF_BPEnable && (fifo.CPReadPointer == fifo.CPBreakpoint);
}

void RunGpu()
{
	SCPFifoStruct &fifo = CommandProcessor::fifo;
	while (fifo.bFF_GPReadEnable && fifo.CPReadWriteDistance && !AtBreakpoint() )
	{
		u8 *uData = Memory::GetPointer(fifo.CPReadPointer);

		FPURoundMode::SaveSIMDState();
		FPURoundMode::LoadDefaultSIMDState();
		ReadDataFromFifo(uData, 32);
		OpcodeDecoder_Run(g_bSkipCurrentFrame);
		FPURoundMode::LoadSIMDState();

		//DEBUG_LOG(COMMANDPROCESSOR, "Fifo wraps to base");

		if (fifo.CPReadPointer == fifo.CPEnd)
			fifo.CPReadPointer = fifo.CPBase;
		else
			fifo.CPReadPointer += 32;

		fifo.CPReadWriteDistance -= 32;
	}
	CommandProcessor::SetCpStatus();
}
