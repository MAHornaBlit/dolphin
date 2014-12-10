// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _VERTEXMANAGER_H
#define _VERTEXMANAGER_H

#include "VertexManagerBase.h"
#include "LineGeometryShader.h"
#include "PointGeometryShader.h"
#include "NativeVertexFormat.h"
#include "D3DBlob.h"

namespace DX11
{

	class D3DVertexFormat : public NativeVertexFormat
	{
		D3D11_INPUT_ELEMENT_DESC m_elems[32];
		UINT m_num_elems;

		DX11::D3DBlob* m_vs_bytecode;
		ID3D11InputLayout* m_layout;

	public:
		D3DVertexFormat() : m_num_elems(0), m_vs_bytecode(NULL), m_layout(NULL) {}
		~D3DVertexFormat() { SAFE_RELEASE(m_vs_bytecode); SAFE_RELEASE(m_layout); }

		void Initialize(const PortableVertexDeclaration &_vtx_decl);
		void SetupVertexPointers();
	};


class VertexManager : public ::VertexManager
{
public:
	VertexManager();
	~VertexManager();

	NativeVertexFormat* CreateNativeVertexFormat();
	void CreateDeviceObjects();
	void DestroyDeviceObjects();
	void ProcessDList();

private:
	
	void PrepareDrawBuffers();
	void Draw(_DisplayListNode::_DrawNode &node);
	void CaptureDraw(u32 stride, _DisplayListNode::_DrawNode &node);
	void DrawNode(_DisplayListNode::_DrawNode &node);

	// temp
	void vFlush();

	u32 m_vertex_buffer_cursor;
	u32 m_vertex_draw_offset;
	u32 m_index_buffer_cursor;
	u32 m_current_vertex_buffer;
	u32 m_current_index_buffer;
	u32 m_triangle_draw_index;
	u32 m_line_draw_index;
	u32 m_point_draw_index;
	typedef ID3D11Buffer* PID3D11Buffer;
	PID3D11Buffer* m_index_buffers;
	PID3D11Buffer* m_vertex_buffers;

	LineGeometryShader m_lineShader;
	PointGeometryShader m_pointShader;
};

}  // namespace

#endif
