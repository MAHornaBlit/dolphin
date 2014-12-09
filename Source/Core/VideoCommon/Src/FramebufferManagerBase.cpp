
#include "FramebufferManagerBase.h"

#include "RenderBase.h"
#include "VideoConfig.h"

FramebufferManagerBase *g_framebuffer_manager;

XFBSourceBase *FramebufferManagerBase::m_realXFBSource[2]; // Only used in Real XFB mode
FramebufferManagerBase::VirtualXFBListType FramebufferManagerBase::m_virtualXFBList1; // Only used in Virtual XFB mode
FramebufferManagerBase::VirtualXFBListType FramebufferManagerBase::m_virtualXFBList2; // Only used in Virtual XFB mode
const XFBSourceBase* FramebufferManagerBase::m_overlappingXFBArray[2][MAX_VIRTUAL_XFB];

unsigned int FramebufferManagerBase::s_last_xfb_width = 1;
unsigned int FramebufferManagerBase::s_last_xfb_height = 1;

extern volatile int g_Eye;

FramebufferManagerBase::FramebufferManagerBase()
{
	m_realXFBSource[0] = NULL;
	m_realXFBSource[1] = NULL;

	// can't hurt
	memset(m_overlappingXFBArray, 0, sizeof(m_overlappingXFBArray));
}

FramebufferManagerBase::~FramebufferManagerBase()
{
	{
		VirtualXFBListType::iterator
			it = m_virtualXFBList1.begin(),
			vlend = m_virtualXFBList1.end();
		for (; it != vlend; ++it)
			delete it->xfbSource;
	}

	{
		VirtualXFBListType::iterator
			it = m_virtualXFBList2.begin(),
			vlend = m_virtualXFBList2.end();
		for (; it != vlend; ++it)
			delete it->xfbSource;
	}

	m_virtualXFBList1.clear();
	m_virtualXFBList2.clear();

	delete m_realXFBSource[0];
	delete m_realXFBSource[1];
}

const XFBSourceBase* const* FramebufferManagerBase::GetXFBSource(u32 xfbAddr, u32 fbWidth, u32 fbHeight, u32 &xfbCount)
{
	if (!g_ActiveConfig.bUseXFB)
		return NULL;

	if (g_ActiveConfig.bUseRealXFB)
		return GetRealXFBSource(xfbAddr, fbWidth, fbHeight, xfbCount);
	else
		return GetVirtualXFBSource(xfbAddr, fbWidth, fbHeight, xfbCount);
}

const XFBSourceBase* const* FramebufferManagerBase::GetRealXFBSource(u32 xfbAddr, u32 fbWidth, u32 fbHeight, u32 &xfbCount)
{
	xfbCount = 1;

	if (!m_realXFBSource[g_Eye])
		m_realXFBSource[g_Eye] = g_framebuffer_manager->CreateXFBSource(fbWidth, fbHeight);

	m_realXFBSource[g_Eye]->srcAddr = xfbAddr;

	m_realXFBSource[g_Eye]->srcWidth = MAX_XFB_WIDTH;
	m_realXFBSource[g_Eye]->srcHeight = MAX_XFB_HEIGHT;

	m_realXFBSource[g_Eye]->texWidth = fbWidth;
	m_realXFBSource[g_Eye]->texHeight = fbHeight;

	// TODO: stuff only used by OGL... :/
	// OpenGL texture coordinates originate at the lower left, which is why
	// sourceRc.top = fbHeight and sourceRc.bottom = 0.
	m_realXFBSource[g_Eye]->sourceRc.left = 0;
	m_realXFBSource[g_Eye]->sourceRc.top = fbHeight;
	m_realXFBSource[g_Eye]->sourceRc.right = fbWidth;
	m_realXFBSource[g_Eye]->sourceRc.bottom = 0;

	// Decode YUYV data from GameCube RAM
	m_realXFBSource[g_Eye]->DecodeToTexture(xfbAddr, fbWidth, fbHeight);

	m_overlappingXFBArray[g_Eye][0] = m_realXFBSource[g_Eye];
	return &m_overlappingXFBArray[g_Eye][0];
}

const XFBSourceBase* const* FramebufferManagerBase::GetVirtualXFBSource(u32 xfbAddr, u32 fbWidth, u32 fbHeight, u32 &xfbCount)
{
	xfbCount = 0;

	FramebufferManagerBase::VirtualXFBListType &vxfbl = g_Eye == 0 ? m_virtualXFBList1 : m_virtualXFBList2;

	if (vxfbl.empty())  // no Virtual XFBs available
		return NULL;

	u32 srcLower = xfbAddr;
	u32 srcUpper = xfbAddr + 2 * fbWidth * fbHeight;

	VirtualXFBListType::reverse_iterator
		it = vxfbl.rbegin(),
		vlend = vxfbl.rend();
	for (; it != vlend; ++it)
	{
		VirtualXFB* vxfb = &*it;

		u32 dstLower = vxfb->xfbAddr;
		u32 dstUpper = vxfb->xfbAddr + 2 * vxfb->xfbWidth * vxfb->xfbHeight;

		if (addrRangesOverlap(srcLower, srcUpper, dstLower, dstUpper))
		{
			m_overlappingXFBArray[g_Eye][xfbCount] = vxfb->xfbSource;
			++xfbCount;
		}
	}

	return &m_overlappingXFBArray[g_Eye][0];
}

void FramebufferManagerBase::CopyToXFB(u32 xfbAddr, u32 fbWidth, u32 fbHeight, const EFBRectangle& sourceRc,float Gamma)
{
	if (g_ActiveConfig.bUseRealXFB)
		g_framebuffer_manager->CopyToRealXFB(xfbAddr, fbWidth, fbHeight, sourceRc,Gamma);
	else
		CopyToVirtualXFB(xfbAddr, fbWidth, fbHeight, sourceRc,Gamma);
}

void FramebufferManagerBase::CopyToVirtualXFB(u32 xfbAddr, u32 fbWidth, u32 fbHeight, const EFBRectangle& sourceRc,float Gamma)
{
	VirtualXFBListType::iterator vxfb = FindVirtualXFB(xfbAddr, fbWidth, fbHeight);

	FramebufferManagerBase::VirtualXFBListType &vxfbl = g_Eye == 0 ? m_virtualXFBList1 : m_virtualXFBList2;

	if (vxfbl.end() == vxfb)
	{
		if (vxfbl.size() < MAX_VIRTUAL_XFB)
		{
			// create a new Virtual XFB and place it at the front of the list
			VirtualXFB v;
			memset(&v, 0, sizeof v);
			vxfbl.push_front(v);
			vxfb = vxfbl.begin();
		}
		else
		{
			// Replace the last virtual XFB
			--vxfb;
		}
	}
	//else // replace existing virtual XFB

	// move this Virtual XFB to the front of the list.
	if (vxfbl.begin() != vxfb)
		vxfbl.splice(vxfbl.begin(), vxfbl, vxfb);

	unsigned int target_width, target_height;
	g_framebuffer_manager->GetTargetSize(&target_width, &target_height, sourceRc);

	// recreate if needed
	if (vxfb->xfbSource && (vxfb->xfbSource->texWidth != target_width || vxfb->xfbSource->texHeight != target_height))
	{
		//delete vxfb->xfbSource;
		//vxfb->xfbSource = NULL;
	}

	if (!vxfb->xfbSource)
	{
		vxfb->xfbSource = g_framebuffer_manager->CreateXFBSource(target_width, target_height);
		vxfb->xfbSource->texWidth = target_width;
		vxfb->xfbSource->texHeight = target_height;
	}

	vxfb->xfbSource->srcAddr = vxfb->xfbAddr = xfbAddr;
	vxfb->xfbSource->srcWidth = vxfb->xfbWidth = fbWidth;
	vxfb->xfbSource->srcHeight = vxfb->xfbHeight = fbHeight;

	vxfb->xfbSource->sourceRc = g_renderer->ConvertEFBRectangle(sourceRc);

	// keep stale XFB data from being used
	ReplaceVirtualXFB();

	// Copy EFB data to XFB and restore render target again
	vxfb->xfbSource->CopyEFB(Gamma);
}

FramebufferManagerBase::VirtualXFBListType::iterator FramebufferManagerBase::FindVirtualXFB(u32 xfbAddr, u32 width, u32 height)
{
	const u32 srcLower = xfbAddr;
	const u32 srcUpper = xfbAddr + 2 * width * height;

	FramebufferManagerBase::VirtualXFBListType &vxfbl = g_Eye == 0 ? m_virtualXFBList1 : m_virtualXFBList2;

	VirtualXFBListType::iterator it = vxfbl.begin();
	for (; it != vxfbl.end(); ++it)
	{
		const u32 dstLower = it->xfbAddr;
		const u32 dstUpper = it->xfbAddr + 2 * it->xfbWidth * it->xfbHeight;

		if (dstLower >= srcLower && dstUpper <= srcUpper)
			break;
	}

	return it;
}

void FramebufferManagerBase::ReplaceVirtualXFB()
{
	FramebufferManagerBase::VirtualXFBListType &vxfbl = g_Eye == 0 ? m_virtualXFBList1 : m_virtualXFBList2;

	VirtualXFBListType::iterator it = vxfbl.begin();

	const s32 srcLower = it->xfbAddr;
	const s32 srcUpper = it->xfbAddr + 2 * it->xfbWidth * it->xfbHeight;
	const s32 lineSize = 2 * it->xfbWidth;

	++it;

	for (; it != vxfbl.end(); ++it)
	{
		s32 dstLower = it->xfbAddr;
		s32 dstUpper = it->xfbAddr + 2 * it->xfbWidth * it->xfbHeight;

		if (dstLower >= srcLower && dstUpper <= srcUpper)
		{
			// Invalidate the data
			it->xfbAddr = 0;
			it->xfbHeight = 0;
			it->xfbWidth = 0;
		}
		else if (addrRangesOverlap(srcLower, srcUpper, dstLower, dstUpper))
		{
			s32 upperOverlap = (srcUpper - dstLower) / lineSize;
			s32 lowerOverlap = (dstUpper - srcLower) / lineSize;

			if (upperOverlap > 0 && lowerOverlap < 0)
			{
				it->xfbAddr += lineSize * upperOverlap;
				it->xfbHeight -= upperOverlap;
			}
			else if (lowerOverlap > 0)
			{
				it->xfbHeight -= lowerOverlap;
			}
		}
	}
}

int FramebufferManagerBase::ScaleToVirtualXfbWidth(int x, unsigned int backbuffer_width)
{
	if (g_ActiveConfig.RealXFBEnabled())
		return x;

	if (g_ActiveConfig.b3DVision)
	{
		// This works, yet the version in the else doesn't. No idea why.
		return x * (int)backbuffer_width / (int)FramebufferManagerBase::LastXfbWidth();
	}
	else
	{
		return x * (int)Renderer::GetTargetRectangle().GetWidth() / (int)FramebufferManagerBase::LastXfbWidth();
	}
}

int FramebufferManagerBase::ScaleToVirtualXfbHeight(int y, unsigned int backbuffer_height)
{
	if (g_ActiveConfig.RealXFBEnabled())
		return y;

	if (g_ActiveConfig.b3DVision)
	{
		// This works, yet the version in the else doesn't. No idea why.
		return y * (int)backbuffer_height / (int)FramebufferManagerBase::LastXfbHeight();
	}
	else
	{
		return y * (int)Renderer::GetTargetRectangle().GetHeight() / (int)FramebufferManagerBase::LastXfbHeight();
	}
}
