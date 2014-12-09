// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "BPMemLoader.h"
#include "EfbCopy.h"
#include "EfbInterface.h"
#include "SWRenderer.h"
#include "TextureEncoder.h"
#include "SWStatistics.h"
#include "SWVideoConfig.h"
#include "DebugUtil.h"
#include "HwRasterizer.h"
#include "SWCommandProcessor.h"
#include "HW/Memmap.h"
#include "Core.h"

namespace EfbCopy
{
	void CopyToXfb()
	{
		GLInterface->Update(); // just updates the render window position and the backbuffer size	

		if (!g_SWVideoConfig.bHwRasterizer)
		{
			// copy to open gl for rendering
			EfbInterface::UpdateColorTexture();
			SWRenderer::DrawTexture(EfbInterface::efbColorTexture, EFB_WIDTH, EFB_HEIGHT);
		}

		SWRenderer::SwapBuffer();
	}

	void CopyToRam()
	{
		if (!g_SWVideoConfig.bHwRasterizer)
		{
			u8 *dest_ptr = Memory::GetPointer(cur_bpmem->copyTexDest << 5);

			TextureEncoder::Encode(dest_ptr);
		}
	}

	void ClearEfb()
	{
		u32 clearColor = (cur_bpmem->clearcolorAR & 0xff) << 24 | cur_bpmem->clearcolorGB << 8 | (cur_bpmem->clearcolorAR & 0xff00) >> 8;

		int left   = cur_bpmem->copyTexSrcXY.x;
		int top    = cur_bpmem->copyTexSrcXY.y;
		int right  = left + cur_bpmem->copyTexSrcWH.x;
		int bottom = top + cur_bpmem->copyTexSrcWH.y;

		for (u16 y = top; y <= bottom; y++)
		{
			for (u16 x = left; x <= right; x++)
			{
				EfbInterface::SetColor(x, y, (u8*)(&clearColor));
				EfbInterface::SetDepth(x, y, cur_bpmem->clearZValue);
			}
		}
	}

	void CopyEfb()
	{
		if (cur_bpmem->triggerEFBCopy.copy_to_xfb)
			DebugUtil::OnFrameEnd();

		if (!g_bSkipCurrentFrame)
		{
			if (cur_bpmem->triggerEFBCopy.copy_to_xfb)
			{
				CopyToXfb();
				Core::Callback_VideoCopiedToXFB(true);

				swstats.frameCount++;
			}
			else
			{
				CopyToRam();
			}

			if (cur_bpmem->triggerEFBCopy.clear)
			{
				if (g_SWVideoConfig.bHwRasterizer)
					HwRasterizer::Clear();
				else
					ClearEfb();
			}
		}
		else
		{
			if (cur_bpmem->triggerEFBCopy.copy_to_xfb)
			{
				// no frame rendered but tell that a frame has finished for frame skip counter
				Core::Callback_VideoCopiedToXFB(false);
			}
		}
	}
}
