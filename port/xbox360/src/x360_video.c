/*
X360_VIDEO.C

The console's screen: its Direct3D 9 device, made once, and shaders
compiled from HLSL on the console (D3DX's compiler, which the SDK's d3dx9
library has: the game's shaders are made at run time from the Xbox's
microcode, so they cannot all be compiled ahead of time).

Before the renderer exists, x360_video_probe checks the pieces it will
stand on, and says so in the log: the device, the compiler, a triangle
drawn with what it compiled, and the frame presented.
*/

#include <xtl.h>
#include <string.h>

#include "x360_port.h"

static D3DDevice *device;

void *x360_video_device(void)
{
	D3DPRESENT_PARAMETERS parameters;
	HRESULT result;

	if (device)
		return device;
	memset(&parameters, 0, sizeof(parameters));
	parameters.BackBufferWidth = 1280;
	parameters.BackBufferHeight = 720;
	parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
	parameters.BackBufferCount = 1;
	parameters.MultiSampleType = D3DMULTISAMPLE_NONE;
	parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
	/* (no depth for the screen: nothing drawn to it tests depth, and its
	depth's EDRAM would be the game's render targets', which follow the back
	buffer: x360_gl.c) */
	parameters.EnableAutoDepthStencil = FALSE;
	parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
	result = Direct3D_CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &parameters,
		&device);
	x360_log("video: device 1280x720: 0x%08lx", (unsigned long)result);
	if (FAILED(result))
		device = NULL;
	return device;
}

/* compiles HLSL source for a profile ("vs_3_0", "ps_3_0"); the microcode,
or NULL with the compiler's message in the log. The caller frees it with
x360_video_shader_free. */
void *x360_video_compile(const char *source, const char *profile, unsigned long *size)
{
	LPD3DXBUFFER shader = NULL, errors = NULL;
	HRESULT result = D3DXCompileShader(source, (UINT)strlen(source), NULL, NULL, "main", profile, 0, &shader,
		&errors, NULL);

	if (FAILED(result) || !shader)
	{
		x360_log("video: %s compile failed (0x%08lx): %s", profile, (unsigned long)result,
			errors ? (const char *)errors->lpVtbl->GetBufferPointer(errors) : "(no message)");
		if (errors)
			errors->lpVtbl->Release(errors);
		return NULL;
	}
	if (errors)
		errors->lpVtbl->Release(errors);
	if (size)
		*size = shader->lpVtbl->GetBufferSize(shader);
	return shader;
}

const void *x360_video_shader_code(void *shader)
{
	LPD3DXBUFFER buffer = shader;

	return buffer->lpVtbl->GetBufferPointer(buffer);
}

void x360_video_shader_free(void *shader)
{
	LPD3DXBUFFER buffer = shader;

	if (buffer)
		buffer->lpVtbl->Release(buffer);
}

void x360_video_probe(void)
{
	static const char vertex_source[] =
		"struct output { float4 position : POSITION; float4 color : COLOR0; };\n"
		"output main(float4 position : POSITION, float4 color : COLOR0)\n"
		"{ output result; result.position = position; result.color = color; return result; }\n";
	static const char pixel_source[] =
		"float4 main(float4 color : COLOR0) : COLOR { return color; }\n";
	static const D3DVERTEXELEMENT9 elements[] =
	{
		{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
		{ 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
		D3DDECL_END()
	};
	static const float triangle[3][8] =
	{
		{ 0.0f, 0.6f, 0.5f, 1.0f, 1.0f, 0.2f, 0.2f, 1.0f },
		{ 0.6f, -0.6f, 0.5f, 1.0f, 0.2f, 1.0f, 0.2f, 1.0f },
		{ -0.6f, -0.6f, 0.5f, 1.0f, 0.2f, 0.2f, 1.0f, 1.0f },
	};
	D3DDevice *probe_device = x360_video_device();
	void *vertex_code, *pixel_code;
	D3DVertexShader *vertex_shader = NULL;
	D3DPixelShader *pixel_shader = NULL;
	D3DVertexDeclaration *declaration;
	unsigned long vertex_size = 0, pixel_size = 0;
	int frame;

	if (!probe_device)
		return;
	vertex_code = x360_video_compile(vertex_source, "vs_3_0", &vertex_size);
	pixel_code = x360_video_compile(pixel_source, "ps_3_0", &pixel_size);
	x360_log("video: compiled a vertex shader (%lu bytes) and a pixel shader (%lu bytes)", vertex_size, pixel_size);
	if (vertex_code)
		vertex_shader = D3DDevice_CreateVertexShader(x360_video_shader_code(vertex_code));
	if (pixel_code)
		pixel_shader = D3DDevice_CreatePixelShader(x360_video_shader_code(pixel_code));
	declaration = D3DDevice_CreateVertexDeclaration(elements);
	x360_log("video: shaders %s, declaration %s", vertex_shader && pixel_shader ? "made" : "not made",
		declaration ? "made" : "not made");
	for (frame = 0; frame < 60; frame++)
	{
		D3DDevice_Clear(probe_device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0x10, 0x20, 0x30),
			1.0f, 0, FALSE);
		if (vertex_shader && pixel_shader && declaration)
		{
			D3DDevice_SetVertexDeclaration(probe_device, declaration);
			D3DDevice_SetVertexShader(probe_device, vertex_shader);
			D3DDevice_SetPixelShader(probe_device, pixel_shader);
			D3DDevice_DrawVerticesUP(probe_device, D3DPT_TRIANGLELIST, 3, triangle, sizeof(triangle[0]));
		}
		D3DDevice_Present(probe_device);
	}
	x360_log("video: probe drew 60 frames");
	x360_video_shader_free(vertex_code);
	x360_video_shader_free(pixel_code);
}
