/*
X360_GL.C

The OpenGL the desktop renderer is written against (port/linux/src/d3d8_gl.c,
the Xbox's Direct3D 8 over OpenGL 4.5, and its texture decoder), on the
console's Direct3D 9. The renderer stays the desktop builds' own; this file
gives it its GL entry points (SDL_GL_GetProcAddress, gl_functions.c), and
x360_glsl.c turns its GLSL into HLSL that the console compiles.

The parts, and how GL's model meets the console's:

- Data. The game's vertices, indices, textures' texels and DXT blocks are
  the Xbox's, little-endian; the console's GPU reads them as they are with
  formats whose endian swap is none (D3DFMT_LE_*, vertex formats with
  GPUENDIAN_NONE). Texels the renderer hands over as GL_BGRA bytes are
  exactly D3DFMT_LE_A8R8G8B8's.
- Buffers keep their bytes in memory of their own; a draw copies the ranges
  it reads into a ring the GPU reads from (write-combined, two frames' worth,
  fenced), and points a vertex or index buffer header at them.
- Render targets. The console draws into EDRAM and resolves into textures.
  Every texture used as a framebuffer attachment has EDRAM of its own
  (allocated once, so switching targets loses nothing), and its texels are
  resolved into it before anything samples it or blits from it.
- Clip space: the renderer asks for glClipControl(GL_UPPER_LEFT,
  GL_ZERO_TO_ONE), which is Direct3D's, so framebuffer coordinates are
  Direct3D's too (row 0 at the top), but for the default framebuffer, whose
  rows GL counts from the bottom (the present blit).
- Programs: a vertex and a fragment shader's HLSL compiled once each; a
  program's uniforms are found in their constant tables, and their values
  kept and set before each draw.

Entry points the renderer calls that are not implemented say so in the log
the first time.
*/

#include <xtl.h>
#include <xgraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_opengl.h>

#include "x360_port.h"
#include "x360_glsl.h"

#define FIRST_USE() first_use(__func__)

/* ---------- the log */

static void first_use(const char *name)
{
	static const char *seen[160];
	static int seen_count;
	int index;

	for (index = 0; index < seen_count; index++)
	{
		if (seen[index] == name)
			return;
	}
	if (seen_count < (int)(sizeof(seen) / sizeof(seen[0])))
		seen[seen_count++] = name;
	x360_log("gl: %s (not implemented)", name);
}

/* bring-up switches (game:\gl.txt: one word each) */
static struct
{
	BOOL loaded;
	BOOL no_draws, constant_stream, no_resolves, log_draws, clear_red, green, no_cull, no_depth, no_blend, probe, dump;
	/* log_frame=N: every draw and clear of frame N; dump_at=N: the picture
	of frame N too; skip_program=N: that program's draws left out */
	unsigned long log_frame, dump_at;
	GLuint skip_programs[16];
	int skip_program_count;
} debug;

static void debug_load(void)
{
	FILE *file;
	char word[64];

	if (debug.loaded)
		return;
	debug.loaded = TRUE;
	file = fopen("game:\\gl.txt", "rb");
	if (!file)
		return;
	while (fscanf(file, "%63s", word) == 1)
	{
		if (!strcmp(word, "no_draws")) debug.no_draws = TRUE;
		if (!strcmp(word, "constant_stream")) debug.constant_stream = TRUE;
		if (!strcmp(word, "no_resolves")) debug.no_resolves = TRUE;
		if (!strcmp(word, "log_draws")) debug.log_draws = TRUE;
		if (!strcmp(word, "clear_red")) debug.clear_red = TRUE;
		if (!strcmp(word, "green")) debug.green = TRUE;
		if (!strcmp(word, "no_cull")) debug.no_cull = TRUE;
		if (!strcmp(word, "no_depth")) debug.no_depth = TRUE;
		if (!strcmp(word, "no_blend")) debug.no_blend = TRUE;
		if (!strcmp(word, "probe")) debug.probe = TRUE;
		if (!strcmp(word, "dump")) debug.dump = TRUE;
		sscanf(word, "log_frame=%lu", &debug.log_frame);
		if (sscanf(word, "dump_at=%lu", &debug.dump_at) == 1)
			debug.dump = TRUE;
		if (debug.skip_program_count < 16 &&
			sscanf(word, "skip_program=%u", &debug.skip_programs[debug.skip_program_count]) == 1)
		{
			debug.skip_program_count++;
		}
	}
	fclose(file);
	x360_log("gl: debug switches: draws %s, constant stream %s, resolves %s",
		debug.no_draws ? "off" : "on", debug.constant_stream ? "on" : "off", debug.no_resolves ? "off" : "on");
}

/* ---------- limits */

#define MAXIMUM_OBJECTS 8192
#define TEXTURE_UNITS 16
#define ATTRIBUTES 16
#define BINDINGS 16
#define MAXIMUM_LEVELS 14
#define RING_VERTEX_SIZE (24 * 1024 * 1024)
#define RING_INDEX_SIZE (4 * 1024 * 1024)
/* EDRAM: 10 MB in tiles of 5,120 bytes; the screen's back buffer is in the
first (x360_video.c's 1280x720, without depth) */
#define EDRAM_TILES 2048

/* ---------- objects */

enum
{
	_object_none,
	_object_texture,
	_object_buffer,
	_object_framebuffer,
	_object_renderbuffer,
	_object_sampler,
	_object_shader,
	_object_program,
	_object_query,
	_object_vertex_array,
};

struct texture
{
	GLenum target;
	GLenum internal_format;
	unsigned long width, height, depth;
	int levels;              /* the console texture's */
	int defined_levels;      /* levels given texels (highest + 1) */
	D3DBaseTexture *d3d;
	D3DFORMAT format;
	BOOL compressed;
	/* sampler state (GL texture parameters) */
	GLint min_filter, mag_filter, wrap_s, wrap_t, wrap_r, base_level, max_level;
	GLfloat border[4], lod_bias, anisotropy;
	/* a framebuffer attachment: its EDRAM, and whether it holds pixels the
	texture does not yet */
	D3DSurface *surface;
	BOOL depth_format;
	BOOL unresolved;
};

/* a buffer's bytes are where the GPU reads them: physical memory of its own
(XPhysicalAlloc), or memory it is an alias of (x360_gl_buffer_alias: the
Xbox window, whose vertices the GPU reads in place) */
struct buffer
{
	unsigned char *data;
	unsigned long size;
	BOOL alias;
};

struct framebuffer
{
	GLuint color;
	GLuint depth;
	GLint color_level;
};

struct renderbuffer
{
	GLenum format;
	unsigned long width, height;
};

struct sampler
{
	GLint min_filter, mag_filter, wrap_s, wrap_t, wrap_r;
	GLfloat border[4], lod_bias, anisotropy;
};

struct shader
{
	GLenum type;
	char *source;
	char *hlsl;
	void *code;              /* ID3DXBuffer */
	LPD3DXCONSTANTTABLE constants;
	D3DVertexShader *vertex;
	D3DPixelShader *pixel;
	BOOL compiled;
	/* the vertex shader's inputs: bit n, location n */
	unsigned long inputs, packed_inputs;
};

#define MAXIMUM_UNIFORMS 64

struct uniform
{
	char name[32];
	/* registers in each stage, or -1 */
	int vertex_register, pixel_register;
	int count;               /* registers (vectors) */
	BOOL sampler;
	int sampler_register;    /* the pixel shader's sampler */
	GLint unit;              /* a sampler's texture unit (glUniform1i) */
};

struct program
{
	GLuint vertex, fragment;
	BOOL linked;
	struct uniform uniforms[MAXIMUM_UNIFORMS];
	int uniform_count;
	/* the constants' values, set before each draw */
	float vertex_constants[256][4];
	float pixel_constants[256][4];
	int vertex_constant_count, pixel_constant_count;
	char log[512];
};

struct query
{
	D3DQuery *d3d;
	BOOL issued;
	DWORD result;
	BOOL available;
};

struct object
{
	int type;
	void *data;
};

static struct object objects[MAXIMUM_OBJECTS];
static GLuint next_name = 1;

static GLuint object_new(int type, void *data)
{
	GLuint name;

	for (name = next_name; name < MAXIMUM_OBJECTS; name++)
	{
		if (objects[name].type == _object_none)
			break;
	}
	if (name >= MAXIMUM_OBJECTS)
	{
		for (name = 1; name < MAXIMUM_OBJECTS && objects[name].type != _object_none; name++)
			;
		if (name >= MAXIMUM_OBJECTS)
		{
			x360_log("gl: out of object names");
			return 0;
		}
	}
	objects[name].type = type;
	objects[name].data = data;
	next_name = name + 1;
	return name;
}

static void *object_get(GLuint name, int type)
{
	if (!name || name >= MAXIMUM_OBJECTS || objects[name].type != type)
		return NULL;
	return objects[name].data;
}

static void generate(GLsizei count, GLuint *names, int type, size_t size)
{
	GLsizei index;

	for (index = 0; index < count; index++)
	{
		void *data = size ? calloc(1, size) : NULL;

		names[index] = object_new(type, data);
	}
}

/* ---------- state */

struct attribute
{
	BOOL enabled;
	GLint size;
	GLenum type;
	BOOL normalized, integer;
	GLuint relative_offset;
	GLuint binding;
	float value[4];
};

struct binding
{
	GLuint buffer;
	GLintptr offset;
	GLsizei stride;
};

static struct
{
	D3DDevice *device;
	BOOL ready;
	/* bindings */
	GLuint array_buffer, element_buffer, copy_write_buffer;
	GLenum active_unit;
	GLuint textures[TEXTURE_UNITS][3];   /* 2D, 3D, cube */
	GLuint samplers[TEXTURE_UNITS];
	GLuint draw_framebuffer, read_framebuffer;
	GLuint renderbuffer;
	GLuint program;
	struct attribute attributes[ATTRIBUTES];
	struct binding bindings[BINDINGS];
	/* fixed-function state */
	BOOL blend, depth_test, stencil_test, cull_face, scissor_test, polygon_offset_fill;
	GLenum blend_source, blend_destination, blend_source_alpha, blend_destination_alpha;
	GLenum blend_equation;
	float blend_color[4];
	GLenum depth_func;
	BOOL depth_mask;
	GLenum stencil_func, stencil_fail, stencil_zfail, stencil_zpass;
	GLint stencil_reference;
	GLuint stencil_mask, stencil_write_mask;
	GLenum cull_mode, front_face;
	BOOL color_mask[4];
	GLint viewport[4], scissor[4];
	double depth_near, depth_far;
	float polygon_offset_factor, polygon_offset_units;
	GLenum polygon_mode;
	float clear_color[4];
	double clear_depth;
	GLint clear_stencil;
	/* the bound console targets (what is in EDRAM's render state) */
	D3DSurface *bound_color, *bound_depth;
	BOOL bound_screen;
	/* EDRAM handed out: tiles from here up */
	DWORD edram_next;
	/* the GPU rings */
	unsigned char *vertex_ring, *index_ring;
	unsigned long vertex_ring_offset, index_ring_offset;
	DWORD frame_fences[2];
	int frame_half;
	/* vertex declarations made */
	struct declaration_cache *declarations;
	/* the present blit's shaders */
	D3DVertexShader *blit_vertex;
	D3DPixelShader *blit_pixel;
	D3DPixelShader *green_pixel;
	struct texture *presented;
	D3DVertexDeclaration *blit_declaration;
	unsigned long frame;
	unsigned long draws;
} gl;

static void state_defaults(void)
{
	int index;

	gl.blend_source = GL_ONE;
	gl.blend_destination = GL_ZERO;
	gl.blend_source_alpha = GL_ONE;
	gl.blend_destination_alpha = GL_ZERO;
	gl.blend_equation = GL_FUNC_ADD;
	gl.depth_func = GL_LESS;
	gl.depth_mask = TRUE;
	gl.stencil_func = GL_ALWAYS;
	gl.stencil_fail = gl.stencil_zfail = gl.stencil_zpass = GL_KEEP;
	gl.stencil_mask = gl.stencil_write_mask = 0xffffffff;
	gl.cull_mode = GL_BACK;
	gl.front_face = GL_CCW;
	gl.color_mask[0] = gl.color_mask[1] = gl.color_mask[2] = gl.color_mask[3] = TRUE;
	gl.depth_near = 0.0;
	gl.depth_far = 1.0;
	gl.polygon_mode = GL_FILL;
	gl.clear_depth = 1.0;
	for (index = 0; index < ATTRIBUTES; index++)
	{
		gl.attributes[index].value[3] = 1.0f;
		gl.attributes[index].binding = (GLuint)index;
		gl.attributes[index].size = 4;
		gl.attributes[index].type = GL_FLOAT;
	}
}

/* ---------- the device */

static const char blit_vertex_source[] =
	"struct output { float4 position : POSITION; float2 uv : TEXCOORD0; };\n"
	"output main(float4 position : POSITION, float2 uv : TEXCOORD0)\n"
	"{ output result; result.position = position; result.uv = uv; return result; }\n";
static const char blit_pixel_source[] =
	"sampler2D source : register(s0);\n"
	"float4 main(float2 uv : TEXCOORD0) : COLOR { return tex2D(source, uv); }\n";

static BOOL gl_ready(void)
{
	void *code;

	if (gl.ready)
		return TRUE;
	gl.device = x360_video_device();
	if (!gl.device)
		return FALSE;
	state_defaults();
	gl.vertex_ring = XPhysicalAlloc(RING_VERTEX_SIZE, MAXULONG_PTR, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
	gl.index_ring = XPhysicalAlloc(RING_INDEX_SIZE, MAXULONG_PTR, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
	if (!gl.vertex_ring || !gl.index_ring)
	{
		x360_log("gl: cannot allocate the vertex and index rings");
		return FALSE;
	}
	/* (after the screen's back buffer: 1280x720, 32 bits, no depth) */
	gl.edram_next = XGSurfaceSize(1280, 720, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE);
	code = x360_video_compile(blit_vertex_source, "vs_3_0", NULL);
	if (code)
	{
		gl.blit_vertex = D3DDevice_CreateVertexShader(x360_video_shader_code(code));
		x360_video_shader_free(code);
	}
	code = x360_video_compile("float4 main() : COLOR { return float4(0.0, 1.0, 0.0, 1.0); }\n", "ps_3_0", NULL);
	if (code)
	{
		gl.green_pixel = D3DDevice_CreatePixelShader(x360_video_shader_code(code));
		x360_video_shader_free(code);
	}
	code = x360_video_compile(blit_pixel_source, "ps_3_0", NULL);
	if (code)
	{
		gl.blit_pixel = D3DDevice_CreatePixelShader(x360_video_shader_code(code));
		x360_video_shader_free(code);
	}
	{
		static const D3DVERTEXELEMENT9 elements[] =
		{
			{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
			{ 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
			D3DDECL_END()
		};

		gl.blit_declaration = D3DDevice_CreateVertexDeclaration(elements);
	}
	gl.ready = TRUE;
	x360_log("gl: ready (EDRAM from tile %lu, rings %u MB and %u MB)", (unsigned long)gl.edram_next,
		RING_VERTEX_SIZE >> 20, RING_INDEX_SIZE >> 20);
	return TRUE;
}

/* ---------- the frame's costs, for the log every few seconds */

static struct
{
	LARGE_INTEGER frequency;
	DWORD last_report;
	unsigned long frames, draws, idle_waits, fence_waits, ring_bytes, textures_deleted;
	double idle_wait_ms, fence_wait_ms, frame_ms;
	LARGE_INTEGER frame_start;
} stats;

static double stats_ms_since(LARGE_INTEGER start)
{
	LARGE_INTEGER now;

	if (!stats.frequency.QuadPart)
		QueryPerformanceFrequency(&stats.frequency);
	QueryPerformanceCounter(&now);
	return (double)(now.QuadPart - start.QuadPart) * 1000.0 / (double)stats.frequency.QuadPart;
}

static void gpu_idle_wait(void)
{
	LARGE_INTEGER start;

	QueryPerformanceCounter(&start);
	IDirect3DDevice9_BlockUntilIdle(gl.device);
	stats.idle_waits++;
	stats.idle_wait_ms += stats_ms_since(start);
}

/* ---------- rings: draw data the GPU reads, two frames' worth */

static void *ring_reserve(unsigned char *ring, unsigned long ring_size, unsigned long *offset, unsigned long size)
{
	unsigned long half = ring_size / 2;
	unsigned long start = gl.frame_half * half;
	unsigned long aligned = (*offset + 31) & ~31UL;

	if (aligned < start)
		aligned = start;
	if (aligned + size > start + half)
	{
		/* (this frame's half is full: the oldest data in it is the frame
		before last's, which its fence has seen through) */
		static BOOL warned;

		if (!warned)
		{
			warned = TRUE;
			x360_log("gl: a frame's draw data passed %lu MB; the ring wraps", half >> 20);
		}
		gpu_idle_wait();
		aligned = start;
		if (size > half)
			return NULL;
	}
	*offset = aligned + size;
	stats.ring_bytes += size;
	return ring + aligned;
}

/* ---------- buffer storage */

/* storage the GPU may still read: freed once the frame that last used it
is done (rings_next_frame) */
#define RETIRED_MAXIMUM 256

static struct
{
	void *storage[2][RETIRED_MAXIMUM];
	int count[2];
} retired;

static void storage_free_now(void *storage)
{
	if (storage)
		XPhysicalFree(storage);
}

static void storage_retire(void *storage)
{
	int half = gl.frame_half;

	if (!storage)
		return;
	if (retired.count[half] == RETIRED_MAXIMUM)
	{
		/* (more than a frame can hold: wait for the GPU, then free them) */
		int index;

		gpu_idle_wait();
		for (index = 0; index < retired.count[half]; index++)
			storage_free_now(retired.storage[half][index]);
		retired.count[half] = 0;
	}
	retired.storage[half][retired.count[half]++] = storage;
}

static void *storage_allocate(unsigned long size)
{
	return XPhysicalAlloc(size ? size : 1, MAXULONG_PTR, 0, PAGE_READWRITE |
		(size >= 1024 * 1024 ? MEM_LARGE_PAGES : 0));
}

/* the CPU's writes to [data, data + size) out of its cache, for the GPU */
static void storage_flush(const void *data, unsigned long size)
{
	unsigned long line = (unsigned long)(UINT_PTR)data & ~127UL;
	unsigned long end = (unsigned long)(UINT_PTR)data + size;

	for (; line < end; line += 128)
		__asm__ volatile ("dcbst 0, %0" : : "r"(line) : "memory");
	__asm__ volatile ("sync" : : : "memory");
}

static void texture_release(struct texture *texture);

#define RETIRED_TEXTURES_MAXIMUM 256

static struct
{
	struct texture *textures[2][RETIRED_TEXTURES_MAXIMUM];
	int count[2];
} retired_textures;

static void retired_textures_free(int half)
{
	int index;

	for (index = 0; index < retired_textures.count[half]; index++)
	{
		texture_release(retired_textures.textures[half][index]);
		free(retired_textures.textures[half][index]);
	}
	retired_textures.count[half] = 0;
}

static void texture_retire(struct texture *texture)
{
	int half = gl.frame_half;

	if (!gl.ready)
	{
		texture_release(texture);
		free(texture);
		return;
	}
	if (retired_textures.count[half] == RETIRED_TEXTURES_MAXIMUM)
	{
		gpu_idle_wait();
		retired_textures_free(half);
	}
	retired_textures.textures[half][retired_textures.count[half]++] = texture;
}

static void rings_next_frame(void)
{
	gl.frame_fences[gl.frame_half] = IDirect3DDevice9_InsertFence(gl.device);
	gl.frame_half ^= 1;
	if (gl.frame_fences[gl.frame_half])
	{
		LARGE_INTEGER start;

		QueryPerformanceCounter(&start);
		D3DDevice_BlockOnFence(gl.frame_fences[gl.frame_half]);
		stats.fence_waits++;
		stats.fence_wait_ms += stats_ms_since(start);
	}
	/* (storage retired two frames ago: the GPU is done with it) */
	retired_textures_free(gl.frame_half);
	{
		int index;

		for (index = 0; index < retired.count[gl.frame_half]; index++)
			storage_free_now(retired.storage[gl.frame_half][index]);
		retired.count[gl.frame_half] = 0;
	}
	gl.vertex_ring_offset = gl.frame_half * (RING_VERTEX_SIZE / 2);
	gl.index_ring_offset = gl.frame_half * (RING_INDEX_SIZE / 2);
}

/* ---------- textures */

static GLuint *bound_texture_slot(GLenum target)
{
	int unit = (int)gl.active_unit;

	if (unit < 0 || unit >= TEXTURE_UNITS)
		unit = 0;
	switch (target)
	{
	case GL_TEXTURE_3D: return &gl.textures[unit][1];
	case GL_TEXTURE_CUBE_MAP:
	case GL_TEXTURE_CUBE_MAP_POSITIVE_X: case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
	case GL_TEXTURE_CUBE_MAP_POSITIVE_Y: case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
	case GL_TEXTURE_CUBE_MAP_POSITIVE_Z: case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
		return &gl.textures[unit][2];
	default: return &gl.textures[unit][0];
	}
}

static struct texture *bound_texture(GLenum target)
{
	return object_get(*bound_texture_slot(target), _object_texture);
}

static void texture_defaults(struct texture *texture)
{
	texture->min_filter = GL_NEAREST_MIPMAP_LINEAR;
	texture->mag_filter = GL_LINEAR;
	texture->wrap_s = texture->wrap_t = texture->wrap_r = GL_REPEAT;
	texture->max_level = 1000;
	texture->anisotropy = 1.0f;
}

static void texture_release(struct texture *texture)
{
	if (texture->d3d)
		IDirect3DResource9_Release((D3DResource *)texture->d3d);
	if (texture->surface)
		IDirect3DResource9_Release((D3DResource *)texture->surface);
	texture->d3d = NULL;
	texture->surface = NULL;
	texture->defined_levels = 0;
}

static int full_levels(unsigned long width, unsigned long height, unsigned long depth)
{
	int levels = 1;

	while ((width > 1 || height > 1 || depth > 1) && levels < MAXIMUM_LEVELS)
	{
		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
		depth = depth > 1 ? depth / 2 : 1;
		levels++;
	}
	return levels;
}

/* the console format for a GL internal format and the data's format and
type; 0 if there is none */
static D3DFORMAT console_format(GLenum internal, GLenum format, GLenum type, BOOL *compressed)
{
	*compressed = FALSE;
	switch (internal)
	{
	case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
	case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
		*compressed = TRUE;
		return (D3DFORMAT)MAKELEFMT(D3DFMT_LIN_DXT1);
	case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
		*compressed = TRUE;
		return (D3DFORMAT)MAKELEFMT(D3DFMT_LIN_DXT2);
	case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
		*compressed = TRUE;
		return (D3DFORMAT)MAKELEFMT(D3DFMT_LIN_DXT4);
	default:
		break;
	}
	/* (the renderer's other textures are RGBA8, given as BGRA or RGBA
	bytes) */
	(void)type;
	if (format == GL_RGBA)
		return (D3DFORMAT)MAKELEFMT(D3DFMT_LIN_A8B8G8R8);
	return (D3DFORMAT)MAKELEFMT(D3DFMT_LIN_A8R8G8B8);
}

/* (re)makes the console texture for this size and format */
static BOOL texture_allocate(struct texture *texture, GLenum target, GLenum internal, unsigned long width,
	unsigned long height, unsigned long depth, D3DFORMAT format, BOOL compressed)
{
	int levels = full_levels(width, height, target == GL_TEXTURE_3D ? depth : 1);
	HRESULT result;

	if (texture->d3d && texture->width == width && texture->height == height && texture->depth == depth &&
		texture->format == format && texture->target == target)
	{
		return TRUE;
	}
	texture_release(texture);
	texture->target = target;
	texture->internal_format = internal;
	texture->width = width;
	texture->height = height;
	texture->depth = depth;
	texture->format = format;
	texture->compressed = compressed;
	texture->levels = levels;
	texture->depth_format = internal == GL_DEPTH24_STENCIL8 || internal == GL_DEPTH_COMPONENT24 ||
		internal == GL_DEPTH_COMPONENT;
	if (texture->depth_format)
		return TRUE;
	if (target == GL_TEXTURE_3D)
		result = IDirect3DDevice9_CreateVolumeTexture(gl.device, width, height, depth, levels, 0, format, 0,
			(D3DVolumeTexture **)&texture->d3d, NULL);
	else if (target == GL_TEXTURE_CUBE_MAP)
		result = IDirect3DDevice9_CreateCubeTexture(gl.device, width, levels, 0, format, 0,
			(D3DCubeTexture **)&texture->d3d, NULL);
	else
		result = IDirect3DDevice9_CreateTexture(gl.device, width, height, levels, 0, format, 0,
			(D3DTexture **)&texture->d3d, NULL);
	if (FAILED(result) || !texture->d3d)
	{
		x360_log("gl: cannot make a %lux%lux%lu texture (format 0x%08lx, 0x%08lx)", width, height, depth,
			(unsigned long)format, (unsigned long)result);
		texture->d3d = NULL;
		return FALSE;
	}
	return TRUE;
}

static unsigned long level_size(unsigned long size, int level)
{
	size >>= level;
	return size ? size : 1;
}

/* copies texels into a level: rows of row_bytes, rows of blocks for a
compressed format */
static void texture_upload(struct texture *texture, GLenum target, int level, unsigned long x, unsigned long y,
	unsigned long width, unsigned long height, unsigned long depth, const void *data, GLenum format)
{
	unsigned long block = texture->compressed ? 4 : 1;
	unsigned long bytes_per_block = texture->compressed ? (texture->format == (D3DFORMAT)MAKELEFMT(D3DFMT_LIN_DXT1) ?
		8 : 16) : 4;
	unsigned long rows = (height + block - 1) / block;
	unsigned long row_bytes = (width + block - 1) / block * bytes_per_block;
	unsigned long slice, row;
	const unsigned char *source = data;

	if (!texture->d3d || !data || level >= texture->levels)
		return;
	(void)format;
	for (slice = 0; slice < (depth ? depth : 1); slice++)
	{
		D3DLOCKED_RECT locked;
		D3DLOCKED_BOX box;
		unsigned char *destination;
		unsigned long pitch;

		if (texture->target == GL_TEXTURE_3D)
		{
			if (slice == 0)
				IDirect3DVolumeTexture9_LockBox((D3DVolumeTexture *)texture->d3d, (UINT)level, &box, NULL, 0);
			destination = (unsigned char *)box.pBits + slice * box.SlicePitch;
			pitch = box.RowPitch;
		}
		else if (texture->target == GL_TEXTURE_CUBE_MAP)
		{
			IDirect3DCubeTexture9_LockRect((D3DCubeTexture *)texture->d3d,
				(D3DCUBEMAP_FACES)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X), (UINT)level, &locked, NULL, 0);
			destination = locked.pBits;
			pitch = locked.Pitch;
		}
		else
		{
			IDirect3DTexture9_LockRect((D3DTexture *)texture->d3d, (UINT)level, &locked, NULL, 0);
			destination = locked.pBits;
			pitch = locked.Pitch;
		}
		destination += (y / block) * pitch + (x / block) * bytes_per_block;
		for (row = 0; row < rows; row++)
		{
			memcpy(destination + row * pitch, source, row_bytes);
			source += row_bytes;
		}
		if (texture->target == GL_TEXTURE_CUBE_MAP)
			IDirect3DCubeTexture9_UnlockRect((D3DCubeTexture *)texture->d3d,
				(D3DCUBEMAP_FACES)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X), (UINT)level);
		else if (texture->target != GL_TEXTURE_3D)
			IDirect3DTexture9_UnlockRect((D3DTexture *)texture->d3d, (UINT)level);
	}
	if (texture->target == GL_TEXTURE_3D)
		IDirect3DVolumeTexture9_UnlockBox((D3DVolumeTexture *)texture->d3d, (UINT)level);
	if (level + 1 > texture->defined_levels)
		texture->defined_levels = level + 1;
}

static GLenum texture_target_of(GLenum target)
{
	if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
		return GL_TEXTURE_CUBE_MAP;
	return target;
}

static void image(GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height, GLsizei depth,
	GLenum format, GLenum type, const void *pixels, BOOL compressed_call)
{
	struct texture *texture = bound_texture(target);
	BOOL compressed;
	D3DFORMAT console;
	GLenum whole = texture_target_of(target);

	if (!texture || !gl_ready())
		return;
	console = compressed_call ? console_format((GLenum)internal, 0, 0, &compressed) :
		console_format((GLenum)internal, format, type, &compressed);
	if (level == 0)
	{
		/* (a render target's texture: its texels come from EDRAM) */
		if (!texture_allocate(texture, whole, (GLenum)internal, (unsigned long)width, (unsigned long)height,
			(unsigned long)(depth ? depth : 1), console, compressed))
		{
			return;
		}
	}
	if (pixels)
		texture_upload(texture, target, level, 0, 0, (unsigned long)width, (unsigned long)height,
			whole == GL_TEXTURE_3D ? (unsigned long)depth : 1, pixels, format);
	else if (level + 1 > texture->defined_levels)
		texture->defined_levels = level + 1;
}

/* averages a level into the next, for A8R8G8B8 textures (glGenerateMipmap) */
static void generate_mipmaps(struct texture *texture)
{
	int level;

	if (!texture->d3d || texture->compressed || texture->target != GL_TEXTURE_2D)
	{
		FIRST_USE();
		return;
	}
	for (level = 1; level < texture->levels; level++)
	{
		D3DLOCKED_RECT above, below;
		unsigned long width = level_size(texture->width, level), height = level_size(texture->height, level);
		unsigned long above_width = level_size(texture->width, level - 1);
		unsigned long above_height = level_size(texture->height, level - 1);
		unsigned long x, y;

		IDirect3DTexture9_LockRect((D3DTexture *)texture->d3d, (UINT)(level - 1), &above, NULL, D3DLOCK_READONLY);
		IDirect3DTexture9_LockRect((D3DTexture *)texture->d3d, (UINT)level, &below, NULL, 0);
		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				unsigned long sum[4] = { 0, 0, 0, 0 };
				int dx, dy, channel;
				unsigned char *out = (unsigned char *)below.pBits + y * below.Pitch + x * 4;

				for (dy = 0; dy < 2; dy++)
				for (dx = 0; dx < 2; dx++)
				{
					unsigned long sx = x * 2 + dx < above_width ? x * 2 + dx : above_width - 1;
					unsigned long sy = y * 2 + dy < above_height ? y * 2 + dy : above_height - 1;
					const unsigned char *in = (const unsigned char *)above.pBits + sy * above.Pitch + sx * 4;

					for (channel = 0; channel < 4; channel++)
						sum[channel] += in[channel];
				}
				for (channel = 0; channel < 4; channel++)
					out[channel] = (unsigned char)((sum[channel] + 2) / 4);
			}
		}
		IDirect3DTexture9_UnlockRect((D3DTexture *)texture->d3d, (UINT)(level - 1));
		IDirect3DTexture9_UnlockRect((D3DTexture *)texture->d3d, (UINT)level);
	}
	texture->defined_levels = texture->levels;
}

/* ---------- EDRAM: a framebuffer attachment's surface */

static BOOL texture_surface(struct texture *texture)
{
	D3DSURFACE_PARAMETERS parameters;
	D3DFORMAT format;
	DWORD tiles;
	HRESULT result;

	if (texture->surface)
		return TRUE;
	format = texture->depth_format ? D3DFMT_D24S8 : D3DFMT_A8R8G8B8;
	tiles = XGSurfaceSize(texture->width, texture->height, format, D3DMULTISAMPLE_NONE);
	if (gl.edram_next + tiles > EDRAM_TILES)
	{
		x360_log("gl: EDRAM is full (%lux%lu wants %lu tiles from %lu): targets share it from the start",
			texture->width, texture->height, (unsigned long)tiles, (unsigned long)gl.edram_next);
		gl.edram_next = XGSurfaceSize(1280, 720, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE);
		if (gl.edram_next + tiles > EDRAM_TILES)
			return FALSE;
	}
	memset(&parameters, 0, sizeof(parameters));
	parameters.Base = gl.edram_next;
	parameters.HierarchicalZBase = 0xFFFFFFFF;
	if (texture->depth_format)
		result = IDirect3DDevice9_CreateDepthStencilSurface(gl.device, texture->width, texture->height, format,
			D3DMULTISAMPLE_NONE, 0, FALSE, &texture->surface, &parameters);
	else
		result = IDirect3DDevice9_CreateRenderTarget(gl.device, texture->width, texture->height, format,
			D3DMULTISAMPLE_NONE, 0, FALSE, &texture->surface, &parameters);
	if (FAILED(result))
	{
		x360_log("gl: cannot make a %lux%lu target in EDRAM (0x%08lx)", texture->width, texture->height,
			(unsigned long)result);
		texture->surface = NULL;
		return FALSE;
	}
	gl.edram_next += tiles;
	/* (its texels: a texture the resolve can write, tiled) */
	if (!texture->depth_format)
	{
		if (texture->d3d)
			IDirect3DResource9_Release((D3DResource *)texture->d3d);
		texture->d3d = NULL;
		texture->format = D3DFMT_A8R8G8B8;
		texture->levels = 1;
		if (FAILED(IDirect3DDevice9_CreateTexture(gl.device, texture->width, texture->height, 1, 0, D3DFMT_A8R8G8B8, 0,
			(D3DTexture **)&texture->d3d, NULL)))
		{
			texture->d3d = NULL;
		}
		texture->defined_levels = 1;
	}
	x360_log("gl: target %lux%lu %s at EDRAM tile %lu", texture->width, texture->height,
		texture->depth_format ? "depth" : "color", (unsigned long)parameters.Base);
	return TRUE;
}

/* the console's bound surfaces, as GL's draw framebuffer has them */
static void bind_targets(void)
{
	struct framebuffer *framebuffer = object_get(gl.draw_framebuffer, _object_framebuffer);
	struct texture *color = framebuffer ? object_get(framebuffer->color, _object_texture) : NULL;
	struct texture *depth = framebuffer ? object_get(framebuffer->depth, _object_texture) : NULL;
	D3DSurface *color_surface = NULL, *depth_surface = NULL;

	if (!framebuffer)
	{
		/* the screen */
		if (!gl.bound_screen)
		{
			D3DSurface *back_buffer = NULL;

			IDirect3DDevice9_GetBackBuffer(gl.device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer);
			IDirect3DDevice9_SetRenderTarget(gl.device, 0, back_buffer);
			IDirect3DDevice9_SetDepthStencilSurface(gl.device, NULL);
			if (back_buffer)
				IDirect3DResource9_Release((D3DResource *)back_buffer);
			gl.bound_screen = TRUE;
			gl.bound_color = gl.bound_depth = NULL;
		}
		return;
	}
	if (color && texture_surface(color))
		color_surface = color->surface;
	if (depth && texture_surface(depth))
		depth_surface = depth->surface;
	if (gl.bound_screen || color_surface != gl.bound_color || depth_surface != gl.bound_depth)
	{
		/* (a target with no color keeps the last one bound: the console
		cannot take none) */
		if (color_surface)
			IDirect3DDevice9_SetRenderTarget(gl.device, 0, color_surface);
		IDirect3DDevice9_SetDepthStencilSurface(gl.device, depth_surface);
		gl.bound_color = color_surface;
		gl.bound_depth = depth_surface;
		gl.bound_screen = FALSE;
	}
}

/* EDRAM's pixels of a color target into its texture, when it has newer */
static void texture_resolve(struct texture *texture)
{
	D3DRECT rectangle;
	D3DPOINT point;

	if (!texture->unresolved || !texture->surface || !texture->d3d || texture->depth_format || debug.no_resolves)
		return;
	texture->unresolved = FALSE;
	if (gl.bound_screen || gl.bound_color != texture->surface)
	{
		IDirect3DDevice9_SetRenderTarget(gl.device, 0, texture->surface);
		IDirect3DDevice9_SetDepthStencilSurface(gl.device, NULL);
	}
	rectangle.x1 = 0;
	rectangle.y1 = 0;
	rectangle.x2 = (LONG)texture->width;
	rectangle.y2 = (LONG)texture->height;
	point.x = 0;
	point.y = 0;
	IDirect3DDevice9_Resolve(gl.device, D3DRESOLVE_RENDERTARGET0, &rectangle, texture->d3d, &point, 0, 0, NULL,
		1.0f, 0, NULL);
	/* the resolve's target stays bound: callers bind theirs again after
	(bound_color can hold a stand-in that is no surface, never to be bound) */
	gl.bound_screen = FALSE;
	gl.bound_color = texture->surface;
	gl.bound_depth = NULL;
}

/* the draw framebuffer's color texture, which a draw or clear makes newer
than its texels */
static void targets_written(void)
{
	struct framebuffer *framebuffer = object_get(gl.draw_framebuffer, _object_framebuffer);
	struct texture *color = framebuffer ? object_get(framebuffer->color, _object_texture) : NULL;

	if (color)
		color->unresolved = TRUE;
}

/* ---------- programs */

static BOOL shader_compile(struct shader *shader)
{
	HRESULT result;
	LPD3DXBUFFER code = NULL, errors = NULL;
	int vertex = shader->type == GL_VERTEX_SHADER;
	const char *location;

	if (shader->compiled)
		return shader->vertex || shader->pixel;
	shader->compiled = TRUE;
	if (!shader->source)
		return FALSE;
	shader->hlsl = x360_glsl_to_hlsl(shader->source, vertex);
	if (!shader->hlsl)
		return FALSE;
	/* (which locations the vertex shader reads, and which are packed) */
	if (vertex)
	{
		for (location = strstr(shader->source, "layout(location = "); location;
			location = strstr(location + 1, "layout(location = "))
		{
			int index = atoi(location + 18);
			const char *after = strchr(location, ')');

			if (after && index >= 0 && index < ATTRIBUTES && !strncmp(after + 1, " in ", 4))
			{
				shader->inputs |= 1UL << index;
				if (!strncmp(after + 5, "uint ", 5))
					shader->packed_inputs |= 1UL << index;
			}
		}
	}
	result = D3DXCompileShader(shader->hlsl, (UINT)strlen(shader->hlsl), NULL, NULL, "main",
		vertex ? "vs_3_0" : "ps_3_0", 0, &code, &errors, &shader->constants);
	if (FAILED(result) || !code)
	{
		static int failures;

		x360_log("gl: a %s shader did not compile: %s", vertex ? "vertex" : "fragment",
			errors ? (const char *)errors->lpVtbl->GetBufferPointer(errors) : "(no message)");
		if (failures++ < 8)
		{
			char path[64];
			FILE *file;

			CreateDirectoryA("game:\\glsl", NULL);
			_snprintf(path, sizeof(path) - 1, "game:\\glsl\\failed%02d.hlsl", failures);
			path[sizeof(path) - 1] = '\0';
			file = fopen(path, "wb");
			if (file)
			{
				fputs(shader->hlsl, file);
				fclose(file);
			}
		}
		if (errors)
			errors->lpVtbl->Release(errors);
		return FALSE;
	}
	if (errors)
		errors->lpVtbl->Release(errors);
	shader->code = code;
	if (vertex)
	{
		/* (only the inputs the compiled shader reads: the game's shaders
		declare all sixteen, and each one fed is a stream, or constants
		copied for every vertex) */
		D3DXSEMANTIC semantics[MAXD3DDECLLENGTH];
		UINT count = 0, index;
		unsigned long used = 0;

		if (SUCCEEDED(D3DXGetShaderInputSemantics((const DWORD *)code->lpVtbl->GetBufferPointer(code), semantics,
			&count)))
		{
			for (index = 0; index < count; index++)
			{
				if (semantics[index].Usage == D3DDECLUSAGE_TEXCOORD && semantics[index].UsageIndex < ATTRIBUTES)
					used |= 1UL << semantics[index].UsageIndex;
			}
			shader->inputs &= used;
			shader->packed_inputs &= used;
		}
		shader->vertex = D3DDevice_CreateVertexShader(code->lpVtbl->GetBufferPointer(code));
	}
	else
		shader->pixel = D3DDevice_CreatePixelShader(code->lpVtbl->GetBufferPointer(code));
	return shader->vertex || shader->pixel;
}

/* a uniform's registers in a stage's constant table, -1 if it has none */
static void constant_registers(LPD3DXCONSTANTTABLE table, const char *name, int *float_register, int *count,
	int *sampler_register)
{
	D3DXHANDLE handle;
	D3DXCONSTANT_DESC description;
	UINT descriptions = 1;

	*float_register = -1;
	*sampler_register = -1;
	if (!table)
		return;
	handle = table->lpVtbl->GetConstantByName(table, 0, name);
	if (!handle)
		return;
	if (FAILED(table->lpVtbl->GetConstantDesc(table, handle, &description, &descriptions)) || !descriptions)
		return;
	if (description.RegisterSet == D3DXRS_SAMPLER)
	{
		*sampler_register = (int)description.RegisterIndex;
	}
	else if (description.RegisterSet == D3DXRS_FLOAT4)
	{
		*float_register = (int)description.RegisterIndex;
		if ((int)description.RegisterCount > *count)
			*count = (int)description.RegisterCount;
	}
}

/* every uniform of a program's two constant tables */
static void program_uniforms(struct program *program, struct shader *vertex, struct shader *fragment)
{
	struct shader *stages[2];
	int stage;

	stages[0] = vertex;
	stages[1] = fragment;
	program->uniform_count = 0;
	for (stage = 0; stage < 2; stage++)
	{
		LPD3DXCONSTANTTABLE table = stages[stage] ? stages[stage]->constants : NULL;
		D3DXCONSTANTTABLE_DESC table_description;
		UINT index;

		if (!table || FAILED(table->lpVtbl->GetDesc(table, &table_description)))
			continue;
		for (index = 0; index < table_description.Constants; index++)
		{
			D3DXHANDLE handle = table->lpVtbl->GetConstant(table, 0, index);
			D3DXCONSTANT_DESC description;
			UINT descriptions = 1;
			struct uniform *uniform = NULL;
			int existing;

			if (!handle || FAILED(table->lpVtbl->GetConstantDesc(table, handle, &description, &descriptions)))
				continue;
			for (existing = 0; existing < program->uniform_count; existing++)
			{
				if (!strcmp(program->uniforms[existing].name, description.Name))
					uniform = &program->uniforms[existing];
			}
			if (!uniform)
			{
				if (program->uniform_count == MAXIMUM_UNIFORMS)
					continue;
				uniform = &program->uniforms[program->uniform_count++];
				memset(uniform, 0, sizeof(*uniform));
				strncpy(uniform->name, description.Name, sizeof(uniform->name) - 1);
				uniform->vertex_register = -1;
				uniform->pixel_register = -1;
				uniform->sampler_register = -1;
			}
			if (description.RegisterSet == D3DXRS_SAMPLER)
			{
				uniform->sampler = TRUE;
				uniform->sampler_register = (int)description.RegisterIndex;
			}
			else if (description.RegisterSet == D3DXRS_FLOAT4)
			{
				if (stage == 0)
					uniform->vertex_register = (int)description.RegisterIndex;
				else
					uniform->pixel_register = (int)description.RegisterIndex;
				if ((int)description.RegisterCount > uniform->count)
					uniform->count = (int)description.RegisterCount;
			}
		}
	}
	(void)constant_registers;
}

/* a location: the uniform's index and the array element */
#define LOCATION(uniform, element) ((GLint)(((uniform) + 1) << 10 | (element)))
#define LOCATION_UNIFORM(location) (((location) >> 10) - 1)
#define LOCATION_ELEMENT(location) ((location) & 1023)

static void uniform_set(GLint location, const float *values, int vectors, int components)
{
	struct program *program = object_get(gl.program, _object_program);
	struct uniform *uniform;
	int index, element;

	if (!program || location < 0)
		return;
	index = LOCATION_UNIFORM(location);
	element = LOCATION_ELEMENT(location);
	if (index < 0 || index >= program->uniform_count)
		return;
	uniform = &program->uniforms[index];
	for (; vectors > 0 && element < uniform->count; vectors--, element++, values += components)
	{
		float vector[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		memcpy(vector, values, sizeof(float) * (size_t)components);
		if (uniform->vertex_register >= 0 && uniform->vertex_register + element < 256)
		{
			memcpy(program->vertex_constants[uniform->vertex_register + element], vector, sizeof(vector));
			if (uniform->vertex_register + element + 1 > program->vertex_constant_count)
				program->vertex_constant_count = uniform->vertex_register + element + 1;
		}
		if (uniform->pixel_register >= 0 && uniform->pixel_register + element < 256)
		{
			memcpy(program->pixel_constants[uniform->pixel_register + element], vector, sizeof(vector));
			if (uniform->pixel_register + element + 1 > program->pixel_constant_count)
				program->pixel_constant_count = uniform->pixel_register + element + 1;
		}
	}
}

/* ---------- draws */

struct declaration_cache
{
	struct declaration_cache *next;
	D3DVERTEXELEMENT9 elements[ATTRIBUTES + 2];
	int count;
	D3DVertexDeclaration *declaration;
};

static DWORD vertex_type(const struct attribute *attribute, BOOL packed)
{
	DWORD type;

	if (packed)
		type = D3DDECLTYPE_HEND3N;
	else if (attribute->type == GL_FLOAT)
		type = attribute->size == 1 ? D3DDECLTYPE_FLOAT1 : attribute->size == 2 ? D3DDECLTYPE_FLOAT2 :
			attribute->size == 3 ? D3DDECLTYPE_FLOAT3 : D3DDECLTYPE_FLOAT4;
	else if (attribute->type == GL_UNSIGNED_BYTE && attribute->size == GL_BGRA)
		type = D3DDECLTYPE_D3DCOLOR;
	else if (attribute->type == GL_UNSIGNED_BYTE)
		type = attribute->normalized ? D3DDECLTYPE_UBYTE4N : D3DDECLTYPE_UBYTE4;
	else if (attribute->type == GL_SHORT)
		type = attribute->size <= 2 ? (attribute->normalized ? D3DDECLTYPE_SHORT2N : D3DDECLTYPE_SHORT2) :
			(attribute->normalized ? D3DDECLTYPE_SHORT4N : D3DDECLTYPE_SHORT4);
	else if (attribute->type == GL_UNSIGNED_SHORT)
		type = attribute->size <= 2 ? (attribute->normalized ? D3DDECLTYPE_USHORT2N : D3DDECLTYPE_USHORT2) :
			(attribute->normalized ? D3DDECLTYPE_USHORT4N : D3DDECLTYPE_USHORT4);
	else
		type = D3DDECLTYPE_FLOAT4;
	/* (the game's data is little-endian) */
	return (type & ~D3DDECLTYPE_ENDIAN_MASK) | (GPUENDIAN_NONE << D3DDECLTYPE_ENDIAN_SHIFT);
}

static unsigned long attribute_bytes(const struct attribute *attribute, BOOL packed)
{
	int size = attribute->size == GL_BGRA ? 4 : attribute->size;

	if (packed)
		return 4;
	switch (attribute->type)
	{
	case GL_FLOAT: case GL_UNSIGNED_INT: case GL_INT: return 4 * (unsigned long)size;
	case GL_SHORT: case GL_UNSIGNED_SHORT: return 2 * (unsigned long)size;
	default: return (unsigned long)size;
	}
}

static D3DVertexDeclaration *declaration_get(const D3DVERTEXELEMENT9 *elements, int count)
{
	struct declaration_cache *entry;

	for (entry = gl.declarations; entry; entry = entry->next)
	{
		if (entry->count == count && !memcmp(entry->elements, elements, sizeof(D3DVERTEXELEMENT9) * (size_t)count))
			return entry->declaration;
	}
	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return NULL;
	memcpy(entry->elements, elements, sizeof(D3DVERTEXELEMENT9) * (size_t)count);
	entry->count = count;
	{
		D3DVERTEXELEMENT9 terminated[ATTRIBUTES + 3];
		static const D3DVERTEXELEMENT9 end = D3DDECL_END();

		memcpy(terminated, elements, sizeof(D3DVERTEXELEMENT9) * (size_t)count);
		terminated[count] = end;
		entry->declaration = D3DDevice_CreateVertexDeclaration(terminated);
	}
	entry->next = gl.declarations;
	gl.declarations = entry;
	return entry->declaration;
}

static D3DCMPFUNC compare_function(GLenum function)
{
	switch (function)
	{
	case GL_NEVER: return D3DCMP_NEVER;
	case GL_LESS: return D3DCMP_LESS;
	case GL_EQUAL: return D3DCMP_EQUAL;
	case GL_LEQUAL: return D3DCMP_LESSEQUAL;
	case GL_GREATER: return D3DCMP_GREATER;
	case GL_NOTEQUAL: return D3DCMP_NOTEQUAL;
	case GL_GEQUAL: return D3DCMP_GREATEREQUAL;
	default: return D3DCMP_ALWAYS;
	}
}

static D3DBLEND blend_factor(GLenum factor)
{
	switch (factor)
	{
	case GL_ZERO: return D3DBLEND_ZERO;
	case GL_ONE: return D3DBLEND_ONE;
	case GL_SRC_COLOR: return D3DBLEND_SRCCOLOR;
	case GL_ONE_MINUS_SRC_COLOR: return D3DBLEND_INVSRCCOLOR;
	case GL_SRC_ALPHA: return D3DBLEND_SRCALPHA;
	case GL_ONE_MINUS_SRC_ALPHA: return D3DBLEND_INVSRCALPHA;
	case GL_DST_ALPHA: return D3DBLEND_DESTALPHA;
	case GL_ONE_MINUS_DST_ALPHA: return D3DBLEND_INVDESTALPHA;
	case GL_DST_COLOR: return D3DBLEND_DESTCOLOR;
	case GL_ONE_MINUS_DST_COLOR: return D3DBLEND_INVDESTCOLOR;
	case GL_SRC_ALPHA_SATURATE: return D3DBLEND_SRCALPHASAT;
	case GL_CONSTANT_COLOR: return D3DBLEND_BLENDFACTOR;
	case GL_ONE_MINUS_CONSTANT_COLOR: return D3DBLEND_INVBLENDFACTOR;
	case GL_CONSTANT_ALPHA: return D3DBLEND_CONSTANTALPHA;
	case GL_ONE_MINUS_CONSTANT_ALPHA: return D3DBLEND_INVCONSTANTALPHA;
	default: return D3DBLEND_ONE;
	}
}

static D3DBLENDOP blend_operation(GLenum equation)
{
	switch (equation)
	{
	case GL_FUNC_SUBTRACT: return D3DBLENDOP_SUBTRACT;
	case GL_FUNC_REVERSE_SUBTRACT: return D3DBLENDOP_REVSUBTRACT;
	case GL_MIN: return D3DBLENDOP_MIN;
	case GL_MAX: return D3DBLENDOP_MAX;
	default: return D3DBLENDOP_ADD;
	}
}

static D3DSTENCILOP stencil_operation(GLenum operation)
{
	switch (operation)
	{
	case GL_ZERO: return D3DSTENCILOP_ZERO;
	case GL_REPLACE: return D3DSTENCILOP_REPLACE;
	case GL_INCR: return D3DSTENCILOP_INCRSAT;
	case GL_DECR: return D3DSTENCILOP_DECRSAT;
	case GL_INVERT: return D3DSTENCILOP_INVERT;
	case GL_INCR_WRAP: return D3DSTENCILOP_INCR;
	case GL_DECR_WRAP: return D3DSTENCILOP_DECR;
	default: return D3DSTENCILOP_KEEP;
	}
}

static D3DTEXTUREADDRESS address_mode(GLint wrap)
{
	switch (wrap)
	{
	case GL_CLAMP_TO_EDGE: return D3DTADDRESS_CLAMP;
	case GL_CLAMP_TO_BORDER: return D3DTADDRESS_BORDER;
	case GL_MIRRORED_REPEAT: return D3DTADDRESS_MIRROR;
	default: return D3DTADDRESS_WRAP;
	}
}

static D3DCOLOR border_color(const GLfloat *color)
{
	int channel;
	unsigned long value[4];

	for (channel = 0; channel < 4; channel++)
	{
		float c = color[channel] < 0.0f ? 0.0f : color[channel] > 1.0f ? 1.0f : color[channel];

		value[channel] = (unsigned long)(c * 255.0f + 0.5f);
	}
	return D3DCOLOR_ARGB(value[3], value[0], value[1], value[2]);
}

static void apply_render_state(void)
{
	D3DDevice *device = gl.device;
	D3DVIEWPORT9 viewport;
	DWORD cull = D3DCULL_NONE;

	IDirect3DDevice9_SetRenderState(device, D3DRS_HALFPIXELOFFSET, TRUE);
	IDirect3DDevice9_SetRenderState(device, D3DRS_HIZENABLE, D3DHIZ_DISABLE);
	IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, gl.blend);
	IDirect3DDevice9_SetRenderState(device, D3DRS_SRCBLEND, blend_factor(gl.blend_source));
	IDirect3DDevice9_SetRenderState(device, D3DRS_DESTBLEND, blend_factor(gl.blend_destination));
	IDirect3DDevice9_SetRenderState(device, D3DRS_BLENDOP, blend_operation(gl.blend_equation));
	IDirect3DDevice9_SetRenderState(device, D3DRS_SEPARATEALPHABLENDENABLE,
		gl.blend_source != gl.blend_source_alpha || gl.blend_destination != gl.blend_destination_alpha);
	IDirect3DDevice9_SetRenderState(device, D3DRS_SRCBLENDALPHA, blend_factor(gl.blend_source_alpha));
	IDirect3DDevice9_SetRenderState(device, D3DRS_DESTBLENDALPHA, blend_factor(gl.blend_destination_alpha));
	IDirect3DDevice9_SetRenderState(device, D3DRS_BLENDOPALPHA, blend_operation(gl.blend_equation));
	{
		D3DCOLOR factor = border_color(gl.blend_color);

		IDirect3DDevice9_SetRenderState(device, D3DRS_BLENDFACTOR, factor);
	}
	IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE, gl.depth_test && gl.bound_depth);
	IDirect3DDevice9_SetRenderState(device, D3DRS_ZFUNC, compare_function(gl.depth_func));
	IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, gl.depth_test && gl.depth_mask && gl.bound_depth);
	IDirect3DDevice9_SetRenderState(device, D3DRS_STENCILENABLE, gl.stencil_test && gl.bound_depth);
	IDirect3DDevice9_SetRenderState(device, D3DRS_STENCILFUNC, compare_function(gl.stencil_func));
	IDirect3DDevice9_SetRenderState(device, D3DRS_STENCILREF, (DWORD)gl.stencil_reference & 0xff);
	IDirect3DDevice9_SetRenderState(device, D3DRS_STENCILMASK, gl.stencil_mask & 0xff);
	IDirect3DDevice9_SetRenderState(device, D3DRS_STENCILWRITEMASK, gl.stencil_write_mask & 0xff);
	IDirect3DDevice9_SetRenderState(device, D3DRS_STENCILFAIL, stencil_operation(gl.stencil_fail));
	IDirect3DDevice9_SetRenderState(device, D3DRS_STENCILZFAIL, stencil_operation(gl.stencil_zfail));
	IDirect3DDevice9_SetRenderState(device, D3DRS_STENCILPASS, stencil_operation(gl.stencil_zpass));
	/* (the winding GL culls is Direct3D's of the same name: measured on
	the console, the menu's draws all culled with the names exchanged) */
	if (gl.cull_face)
	{
		GLenum culled = gl.cull_mode == GL_FRONT ? gl.front_face : gl.front_face == GL_CCW ? GL_CW : GL_CCW;

		cull = gl.cull_mode == GL_FRONT_AND_BACK ? D3DCULL_NONE : culled == GL_CCW ? D3DCULL_CCW : D3DCULL_CW;
	}
	IDirect3DDevice9_SetRenderState(device, D3DRS_CULLMODE, cull);
	IDirect3DDevice9_SetRenderState(device, D3DRS_COLORWRITEENABLE,
		(gl.color_mask[0] ? D3DCOLORWRITEENABLE_RED : 0) | (gl.color_mask[1] ? D3DCOLORWRITEENABLE_GREEN : 0) |
		(gl.color_mask[2] ? D3DCOLORWRITEENABLE_BLUE : 0) | (gl.color_mask[3] ? D3DCOLORWRITEENABLE_ALPHA : 0));
	IDirect3DDevice9_SetRenderState(device, D3DRS_FILLMODE, gl.polygon_mode == GL_LINE ? D3DFILL_WIREFRAME :
		gl.polygon_mode == GL_POINT ? D3DFILL_POINT : D3DFILL_SOLID);
	{
		float bias = gl.polygon_offset_fill ? gl.polygon_offset_units / 16777216.0f : 0.0f;
		float slope = gl.polygon_offset_fill ? gl.polygon_offset_factor : 0.0f;

		IDirect3DDevice9_SetRenderState(device, D3DRS_DEPTHBIAS, *(DWORD *)&bias);
		IDirect3DDevice9_SetRenderState(device, D3DRS_SLOPESCALEDEPTHBIAS, *(DWORD *)&slope);
	}
	IDirect3DDevice9_SetRenderState(device, D3DRS_SCISSORTESTENABLE, gl.scissor_test);
	if (gl.scissor_test)
	{
		RECT rectangle;

		rectangle.left = gl.scissor[0];
		rectangle.top = gl.scissor[1];
		rectangle.right = gl.scissor[0] + gl.scissor[2];
		rectangle.bottom = gl.scissor[1] + gl.scissor[3];
		IDirect3DDevice9_SetScissorRect(device, &rectangle);
	}
	if (debug.no_cull)
		IDirect3DDevice9_SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
	if (debug.no_depth)
	{
		IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE, FALSE);
		IDirect3DDevice9_SetRenderState(device, D3DRS_STENCILENABLE, FALSE);
	}
	if (debug.no_blend)
		IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, FALSE);
	viewport.X = (DWORD)(gl.viewport[0] < 0 ? 0 : gl.viewport[0]);
	viewport.Y = (DWORD)(gl.viewport[1] < 0 ? 0 : gl.viewport[1]);
	viewport.Width = (DWORD)gl.viewport[2];
	viewport.Height = (DWORD)gl.viewport[3];
	viewport.MinZ = (float)gl.depth_near;
	viewport.MaxZ = (float)gl.depth_far;
	IDirect3DDevice9_SetViewport(device, &viewport);
}

static void apply_sampler(int sampler_register, struct texture *texture, const struct sampler *sampler)
{
	D3DDevice *device = gl.device;
	GLint min_filter = sampler ? sampler->min_filter : texture->min_filter;
	GLint mag_filter = sampler ? sampler->mag_filter : texture->mag_filter;
	GLfloat anisotropy = sampler ? sampler->anisotropy : texture->anisotropy;
	GLfloat lod_bias = sampler ? sampler->lod_bias : texture->lod_bias;
	const GLfloat *border = sampler ? sampler->border : texture->border;
	BOOL mipmapped = min_filter != GL_NEAREST && min_filter != GL_LINEAR;
	int top_level = texture->defined_levels > 0 ? texture->defined_levels - 1 : 0;

	if (texture->max_level < top_level)
		top_level = texture->max_level;
	IDirect3DDevice9_SetTexture(device, (DWORD)sampler_register, texture->d3d);
	IDirect3DDevice9_SetSamplerState(device, (DWORD)sampler_register, D3DSAMP_ADDRESSU,
		address_mode(sampler ? sampler->wrap_s : texture->wrap_s));
	IDirect3DDevice9_SetSamplerState(device, (DWORD)sampler_register, D3DSAMP_ADDRESSV,
		address_mode(sampler ? sampler->wrap_t : texture->wrap_t));
	IDirect3DDevice9_SetSamplerState(device, (DWORD)sampler_register, D3DSAMP_ADDRESSW,
		address_mode(sampler ? sampler->wrap_r : texture->wrap_r));
	IDirect3DDevice9_SetSamplerState(device, (DWORD)sampler_register, D3DSAMP_MAGFILTER,
		mag_filter == GL_NEAREST ? D3DTEXF_POINT : D3DTEXF_LINEAR);
	IDirect3DDevice9_SetSamplerState(device, (DWORD)sampler_register, D3DSAMP_MINFILTER,
		anisotropy > 1.0f ? D3DTEXF_ANISOTROPIC :
		(min_filter == GL_NEAREST || min_filter == GL_NEAREST_MIPMAP_NEAREST ||
		min_filter == GL_NEAREST_MIPMAP_LINEAR) ? D3DTEXF_POINT : D3DTEXF_LINEAR);
	IDirect3DDevice9_SetSamplerState(device, (DWORD)sampler_register, D3DSAMP_MIPFILTER,
		!mipmapped || top_level == 0 ? D3DTEXF_NONE :
		(min_filter == GL_NEAREST_MIPMAP_NEAREST || min_filter == GL_LINEAR_MIPMAP_NEAREST) ? D3DTEXF_POINT :
		D3DTEXF_LINEAR);
	IDirect3DDevice9_SetSamplerState(device, (DWORD)sampler_register, D3DSAMP_MAXANISOTROPY,
		(DWORD)(anisotropy > 1.0f ? anisotropy : 1.0f));
	IDirect3DDevice9_SetSamplerState(device, (DWORD)sampler_register, D3DSAMP_MIPMAPLODBIAS, *(DWORD *)&lod_bias);
	IDirect3DDevice9_SetSamplerState(device, (DWORD)sampler_register, D3DSAMP_MAXMIPLEVEL, 0);
	IDirect3DDevice9_SetSamplerState(device, (DWORD)sampler_register, D3DSAMP_MINMIPLEVEL, (DWORD)top_level);
	IDirect3DDevice9_SetSamplerState(device, (DWORD)sampler_register, D3DSAMP_BORDERCOLOR, border_color(border));
}

/* the texture a program's sampler reads, by its unit */
static struct texture *unit_texture(GLint unit)
{
	struct texture *texture;
	int slot;

	if (unit < 0 || unit >= TEXTURE_UNITS)
		return NULL;
	for (slot = 0; slot < 3; slot++)
	{
		texture = object_get(gl.textures[unit][slot], _object_texture);
		if (texture && texture->d3d)
			return texture;
	}
	return NULL;
}

/* everything a draw needs but its vertices; FALSE if it cannot be drawn */
/* the draw framebuffer's targets and the textures sampled, for the log */
static void draw_log_targets_and_textures(struct program *program)
{
	struct framebuffer *framebuffer = object_get(gl.draw_framebuffer, _object_framebuffer);
	struct texture *color = framebuffer ? object_get(framebuffer->color, _object_texture) : NULL;
	struct texture *depth = framebuffer ? object_get(framebuffer->depth, _object_texture) : NULL;
	char textures[160];
	int index, length = 0;

	textures[0] = 0;
	for (index = 0; index < program->uniform_count && length < (int)sizeof(textures) - 24; index++)
	{
		struct uniform *uniform = &program->uniforms[index];
		struct texture *texture;

		if (!uniform->sampler || uniform->sampler_register < 0)
			continue;
		texture = unit_texture(uniform->unit);
		length += _snprintf(textures + length, sizeof(textures) - length, " s%d=%lux%lu%s", uniform->sampler_register,
			texture ? texture->width : 0, texture ? texture->height : 0,
			texture && texture->surface ? "(target)" : "");
	}
	/* (each program's shaders once, to game:\glsl\p<program>.vs.glsl and so on) */
	{
		static GLuint dumped[256];
		static int dumped_count;
		int seen = 0, which;

		for (which = 0; which < dumped_count; which++)
			seen |= dumped[which] == gl.program;
		if (!seen && dumped_count < 256)
		{
			struct shader *stages[2];

			dumped[dumped_count++] = gl.program;
			/* (and its constants and uniforms as this draw has them) */
			{
				char path[64];
				FILE *file;
				int row, index;

				CreateDirectoryA("game:\\glsl", NULL);
				_snprintf(path, sizeof(path) - 1, "game:\\glsl\\p%u.constants.txt", gl.program);
				path[sizeof(path) - 1] = '\0';
				file = fopen(path, "wb");
				if (file)
				{
					for (index = 0; index < program->uniform_count; index++)
						fprintf(file, "uniform %s: vertex %d, pixel %d, sampler %d, count %d\n",
							program->uniforms[index].name, program->uniforms[index].vertex_register,
							program->uniforms[index].pixel_register, program->uniforms[index].sampler_register,
							program->uniforms[index].count);
					for (row = 0; row < program->vertex_constant_count; row++)
						fprintf(file, "vs c%d: %g %g %g %g\n", row, program->vertex_constants[row][0],
							program->vertex_constants[row][1], program->vertex_constants[row][2],
							program->vertex_constants[row][3]);
					for (row = 0; row < program->pixel_constant_count; row++)
						fprintf(file, "ps c%d: %g %g %g %g\n", row, program->pixel_constants[row][0],
							program->pixel_constants[row][1], program->pixel_constants[row][2],
							program->pixel_constants[row][3]);
					fclose(file);
				}
			}
			stages[0] = object_get(program->vertex, _object_shader);
			stages[1] = object_get(program->fragment, _object_shader);
			CreateDirectoryA("game:\\glsl", NULL);
			for (which = 0; which < 2; which++)
			{
				int text;

				for (text = 0; text < 2 && stages[which]; text++)
				{
					const char *body = text ? stages[which]->hlsl : stages[which]->source;
					char path[64];
					FILE *file;

					if (!body)
						continue;
					_snprintf(path, sizeof(path) - 1, "game:\\glsl\\p%u.%s.%s", gl.program, which ? "ps" : "vs",
						text ? "hlsl" : "glsl");
					path[sizeof(path) - 1] = '\0';
					file = fopen(path, "wb");
					if (file)
					{
						fputs(body, file);
						fclose(file);
					}
				}
			}
		}
	}
	x360_log("gl:   program %u, framebuffer %u: color %lux%lu, depth %lux%lu; textures%s", gl.program,
		gl.draw_framebuffer, color ? color->width : 0, color ? color->height : 0, depth ? depth->width : 0,
		depth ? depth->height : 0, textures);
}

/* whether this draw goes in the log (log_draws: the first 40; log_frame) */
static BOOL draw_logged(void)
{
	return (debug.log_draws && gl.draws < 40) || (debug.log_frame && gl.frame == debug.log_frame);
}

static BOOL program_skipped(GLuint program)
{
	int index;

	for (index = 0; index < debug.skip_program_count; index++)
	{
		if (debug.skip_programs[index] == program)
			return TRUE;
	}
	return FALSE;
}

static BOOL prepare_draw(struct program **program_out, struct shader **vertex_out)
{
	struct program *program = object_get(gl.program, _object_program);
	struct shader *vertex, *fragment;
	int index;

	if (!gl.ready || !program || !program->linked || program_skipped(gl.program))
		return FALSE;
	vertex = object_get(program->vertex, _object_shader);
	fragment = object_get(program->fragment, _object_shader);
	if (!vertex || !fragment || !vertex->vertex || !fragment->pixel)
		return FALSE;
	/* textures first: a resolve rebinds the targets */
	for (index = 0; index < program->uniform_count; index++)
	{
		struct uniform *uniform = &program->uniforms[index];
		struct texture *texture;

		if (!uniform->sampler || uniform->sampler_register < 0)
			continue;
		texture = unit_texture(uniform->unit);
		if (texture)
			texture_resolve(texture);
	}
	bind_targets();
	for (index = 0; index < program->uniform_count; index++)
	{
		struct uniform *uniform = &program->uniforms[index];
		struct texture *texture;

		if (!uniform->sampler || uniform->sampler_register < 0)
			continue;
		texture = unit_texture(uniform->unit);
		if (texture)
			apply_sampler(uniform->sampler_register, texture,
				object_get(gl.samplers[uniform->unit], _object_sampler));
		else
			IDirect3DDevice9_SetTexture(gl.device, (DWORD)uniform->sampler_register, NULL);
	}
	apply_render_state();
	IDirect3DDevice9_SetVertexShader(gl.device, vertex->vertex);
	IDirect3DDevice9_SetPixelShader(gl.device, debug.green && gl.green_pixel ? gl.green_pixel : fragment->pixel);
	if (draw_logged())
	{
		x360_log("gl:   viewport %d %d %d %d, scissor %d (%d %d %d %d), depth %d func 0x%x mask %d, blend %d 0x%x 0x%x, "
			"cull %d 0x%x 0x%x, colors %d%d%d%d, constants %d/%d, target %p/%p",
			gl.viewport[0], gl.viewport[1], gl.viewport[2], gl.viewport[3], gl.scissor_test, gl.scissor[0],
			gl.scissor[1], gl.scissor[2], gl.scissor[3], gl.depth_test, gl.depth_func, gl.depth_mask, gl.blend,
			gl.blend_source, gl.blend_destination, gl.cull_face, gl.cull_mode, gl.front_face, gl.color_mask[0],
			gl.color_mask[1], gl.color_mask[2], gl.color_mask[3], program->vertex_constant_count,
			program->pixel_constant_count, (void *)gl.bound_color, (void *)gl.bound_depth);
		draw_log_targets_and_textures(program);
	}
	if (program->vertex_constant_count)
		IDirect3DDevice9_SetVertexShaderConstantF(gl.device, 0, &program->vertex_constants[0][0],
			(UINT)program->vertex_constant_count);
	if (program->pixel_constant_count)
		IDirect3DDevice9_SetPixelShaderConstantF(gl.device, 0, &program->pixel_constants[0][0],
			(UINT)program->pixel_constant_count);
	*program_out = program;
	*vertex_out = vertex;
	return TRUE;
}

/* the vertex declaration and streams for vertices [0, vertex_end) of the
attributes the vertex shader reads */
/* (a draw's base vertex, applied to its streams' addresses: the GPU's own
base vertex does not take a negative one, which the renderer's draws from
the mirror use) */
static long draw_base_vertex;

static BOOL setup_vertices(struct shader *vertex, unsigned long vertex_start, unsigned long vertex_end)
{
	D3DVERTEXELEMENT9 elements[ATTRIBUTES + 1];
	static D3DVertexBuffer stream_headers[2][ATTRIBUTES + 1];
	D3DVertexBuffer *streams = stream_headers[gl.frame_half];
	int element_count = 0, stream_count = 0;
	int stream_of_binding[BINDINGS];
	float *constants = NULL;
	int constant_stream = -1, constant_count = 0;
	int index;

	for (index = 0; index < BINDINGS; index++)
		stream_of_binding[index] = -1;
	for (index = 0; index < ATTRIBUTES; index++)
	{
		struct attribute *attribute = &gl.attributes[index];
		BOOL packed = (vertex->packed_inputs >> index) & 1;
		D3DVERTEXELEMENT9 *element;

		if (!((vertex->inputs >> index) & 1))
			continue;
		element = &elements[element_count++];
		element->Method = D3DDECLMETHOD_DEFAULT;
		element->Usage = D3DDECLUSAGE_TEXCOORD;
		element->UsageIndex = (BYTE)index;
		if (attribute->enabled && attribute->binding < BINDINGS && gl.bindings[attribute->binding].buffer &&
			((attribute->relative_offset & 3) || (gl.bindings[attribute->binding].stride & 3)))
		{
			/* (the GPU fetches whole words: an attribute not on a word of its
			vertex, such as a model vertex's node weight at byte 30, is copied
			to a stream of its own, a word a vertex) */
			struct binding *binding = &gl.bindings[attribute->binding];
			struct buffer *buffer = object_get(binding->buffer, _object_buffer);
			unsigned long element_bytes = attribute_bytes(attribute, packed);
			unsigned long slot = (element_bytes + 3) & ~3UL;
			unsigned long stride = (unsigned long)binding->stride;
			unsigned long vertex_index;
			unsigned char *copy;
			int stream;

			if (!buffer || !buffer->data || !vertex_end || element_bytes > 16)
				return FALSE;
			copy = ring_reserve(gl.vertex_ring, RING_VERTEX_SIZE, &gl.vertex_ring_offset, slot * vertex_end);
			if (!copy)
				return FALSE;
			memset(copy, 0, slot * vertex_end);
			for (vertex_index = 0; vertex_index < vertex_end; vertex_index++)
			{
				long source = (long)binding->offset + ((long)vertex_index + draw_base_vertex) * (long)stride +
					(long)attribute->relative_offset;

				if (source >= 0 && (unsigned long)source + element_bytes <= buffer->size)
					memcpy(copy + vertex_index * slot, buffer->data + source, element_bytes);
			}
			stream = stream_count++;
			XGSetVertexBufferHeader(slot * vertex_end, 0, 0, 0, &streams[stream]);
			XGOffsetResourceAddress((D3DResource *)&streams[stream], copy);
			IDirect3DDevice9_SetStreamSource(gl.device, (UINT)stream, &streams[stream], 0, (UINT)slot);
			element->Stream = (WORD)stream;
			element->Offset = 0;
			element->Type = vertex_type(attribute, packed);
		}
		else if (attribute->enabled && attribute->binding < BINDINGS && gl.bindings[attribute->binding].buffer)
		{
			struct binding *binding = &gl.bindings[attribute->binding];
			int stream = stream_of_binding[attribute->binding];

			if (stream < 0)
			{
				struct buffer *buffer = object_get(binding->buffer, _object_buffer);
				unsigned long stride = (unsigned long)binding->stride;
				unsigned long bytes;
				unsigned char *copy;

				if (!buffer || !buffer->data || (unsigned long)binding->offset >= buffer->size)
					return FALSE;
				/* (read where it is: buffers are in memory the GPU reads) */
				copy = buffer->data + binding->offset;
				bytes = buffer->size - (unsigned long)binding->offset;
				if ((UINT_PTR)copy & 3)
				{
					/* (a stream starts on 4 bytes: an odd one is copied) */
					bytes = stride ? stride * vertex_end : 64;
					if ((unsigned long)binding->offset + bytes > buffer->size)
						bytes = buffer->size - (unsigned long)binding->offset;
					copy = ring_reserve(gl.vertex_ring, RING_VERTEX_SIZE, &gl.vertex_ring_offset, bytes);
					if (!copy)
						return FALSE;
					memcpy(copy, buffer->data + binding->offset, bytes);
				}
				if (draw_base_vertex)
				{
					copy += draw_base_vertex * (long)stride;
					bytes = (unsigned long)((long)bytes - draw_base_vertex * (long)stride);
				}
				stream = stream_count++;
				stream_of_binding[attribute->binding] = stream;
				XGSetVertexBufferHeader(bytes, 0, 0, 0, &streams[stream]);
				XGOffsetResourceAddress((D3DResource *)&streams[stream], copy);
				IDirect3DDevice9_SetStreamSource(gl.device, (UINT)stream, &streams[stream], 0, (UINT)stride);
			}
			element->Stream = (WORD)stream;
			element->Offset = (WORD)attribute->relative_offset;
			element->Type = vertex_type(attribute, packed);
			if (draw_logged())
			{
				struct buffer *buffer = object_get(binding->buffer, _object_buffer);
				unsigned long at = (unsigned long)binding->offset + vertex_start * (unsigned long)binding->stride +
					attribute->relative_offset;
				const unsigned char *bytes = buffer && buffer->data && at + 16 <= buffer->size ? buffer->data + at : NULL;

				x360_log("gl:     attribute %d: size %d type 0x%x%s%s%s, offset %u, stride %d, declared 0x%08lx; "
					"vertex %lu: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x", index,
					attribute->size, attribute->type, attribute->normalized ? " normalized" : "",
					attribute->integer ? " integer" : "", packed ? " packed" : "", attribute->relative_offset,
					binding->stride, (unsigned long)element->Type, vertex_start,
					bytes ? bytes[0] : 0, bytes ? bytes[1] : 0, bytes ? bytes[2] : 0, bytes ? bytes[3] : 0,
					bytes ? bytes[4] : 0, bytes ? bytes[5] : 0, bytes ? bytes[6] : 0, bytes ? bytes[7] : 0,
					bytes ? bytes[8] : 0, bytes ? bytes[9] : 0, bytes ? bytes[10] : 0, bytes ? bytes[11] : 0,
					bytes ? bytes[12] : 0, bytes ? bytes[13] : 0, bytes ? bytes[14] : 0, bytes ? bytes[15] : 0);
			}
		}
		else
		{
			/* a constant attribute: from one vertex, read by every vertex */
			if (!constants)
			{
				constants = ring_reserve(gl.vertex_ring, RING_VERTEX_SIZE, &gl.vertex_ring_offset,
					ATTRIBUTES * 16);
				if (!constants)
					return FALSE;
				constant_stream = stream_count++;
			}
			/* (packed: only the constant attributes, one after another) */
			memcpy(constants + constant_count * 4, attribute->value, 16);
			if (draw_logged())
				x360_log("gl:     attribute %d: constant %g %g %g %g", index, attribute->value[0], attribute->value[1],
					attribute->value[2], attribute->value[3]);
			element->Stream = (WORD)constant_stream;
			element->Offset = (WORD)(constant_count * 16);
			constant_count++;
			element->Type = (D3DDECLTYPE_FLOAT4 & ~D3DDECLTYPE_ENDIAN_MASK) |
				(GPUENDIAN_NONE << D3DDECLTYPE_ENDIAN_SHIFT);
		}
	}
	if (constants)
	{
		/* (big-endian floats written here: swapped to the GPU's order, as the
		format has none of its own) */
		int value;
		unsigned long stride = 0, bytes = (unsigned long)constant_count * 16;
		float *data = constants;

		for (value = 0; value < constant_count * 4; value++)
		{
			unsigned long word;

			memcpy(&word, &constants[value], 4);
			word = __builtin_bswap32(word);
			memcpy(&constants[value], &word, 4);
		}
		if (!debug.constant_stream)
		{
			/* (every vertex its own copy, rather than one read by all) */
			unsigned long vertex_index;

			unsigned long vertices;

			/* (only the vertices drawn: the stream starts that many before
			its copies, which the draw never reads) */
			stride = (unsigned long)constant_count * 16;
			if (vertex_end <= vertex_start)
				vertex_end = vertex_start + 1;
			vertices = vertex_end - vertex_start;
			data = ring_reserve(gl.vertex_ring, RING_VERTEX_SIZE, &gl.vertex_ring_offset, stride * vertices);
			if (!data)
				return FALSE;
			for (vertex_index = 0; vertex_index < vertices; vertex_index++)
				memcpy((unsigned char *)data + vertex_index * stride, constants, stride);
			if ((UINT_PTR)data < vertex_start * stride + 0x100000)
			{
				x360_log("gl: a draw's constant attributes cannot start %lu vertices back", vertex_start);
				return FALSE;
			}
			data = (float *)((unsigned char *)data - vertex_start * stride);
			bytes = stride * vertex_end;
		}
		XGSetVertexBufferHeader(bytes, 0, 0, 0, &streams[constant_stream]);
		XGOffsetResourceAddress((D3DResource *)&streams[constant_stream], data);
		IDirect3DDevice9_SetStreamSource(gl.device, (UINT)constant_stream, &streams[constant_stream], 0,
			(UINT)stride);
	}
	IDirect3DDevice9_SetVertexDeclaration(gl.device, declaration_get(elements, element_count));
	return TRUE;
}

static D3DPRIMITIVETYPE primitive_type(GLenum mode, unsigned long *count)
{
	switch (mode)
	{
	case GL_POINTS: return D3DPT_POINTLIST;
	case GL_LINES: return D3DPT_LINELIST;
	case GL_LINE_STRIP: return D3DPT_LINESTRIP;
	case GL_TRIANGLE_STRIP: return D3DPT_TRIANGLESTRIP;
	case GL_TRIANGLE_FAN: return D3DPT_TRIANGLEFAN;
	default: *count -= *count % 3; return D3DPT_TRIANGLELIST;
	}
}

static void draw(GLenum mode, unsigned long first, unsigned long count, GLenum index_type, const void *indices,
	long base_vertex)
{
	struct program *program;
	struct shader *vertex;
	D3DPRIMITIVETYPE type;
	unsigned long vertex_end = first + count;

	debug_load();
	if (debug.no_draws || !count || !prepare_draw(&program, &vertex))
		return;
	type = primitive_type(mode, &count);
	if (!count)
		return;
	if (index_type)
	{
		struct buffer *buffer = object_get(gl.element_buffer, _object_buffer);
		unsigned long index_bytes = index_type == GL_UNSIGNED_INT ? 4 : 2;
		unsigned long offset = (unsigned long)(UINT_PTR)indices;
		const unsigned char *source;
		void *copy;
		static D3DIndexBuffer index_headers[2];
		unsigned long index, highest = 0, lowest = ~0UL;

		if (!buffer || !buffer->data || offset + count * index_bytes > buffer->size)
			return;
		source = buffer->data + offset;
		for (index = 0; index < count; index++)
		{
			/* (indices are the machine's words: the game's, and a model's
			made so as it loads, rasterizer_xbox_hardware_geometry.c) */
			unsigned long value = index_bytes == 4 ? ((const unsigned long *)source)[index] :
				((const unsigned short *)source)[index];

			if (index_bytes == 2 && value == 0xffff)
				continue;
			if (value > highest)
				highest = value;
			if (value < lowest)
				lowest = value;
		}
		if (lowest > highest)
			lowest = highest;
		/* (vertices by index: the base is in the streams' addresses) */
		vertex_end = highest + 1;
		draw_base_vertex = base_vertex;
		copy = ring_reserve(gl.index_ring, RING_INDEX_SIZE, &gl.index_ring_offset, count * index_bytes);
		if (!copy || !setup_vertices(vertex, lowest, vertex_end))
		{
			draw_base_vertex = 0;
			return;
		}
		draw_base_vertex = 0;
		memcpy(copy, source, count * index_bytes);
		XGSetIndexBufferHeader(count * index_bytes, 0, index_bytes == 4 ? D3DFMT_INDEX32 : D3DFMT_INDEX16, 0,
			0, &index_headers[gl.frame_half]);
		XGOffsetResourceAddress((D3DResource *)&index_headers[gl.frame_half], copy);
		IDirect3DDevice9_SetIndices(gl.device, &index_headers[gl.frame_half]);
		if (draw_logged())
			x360_log("gl: draw %lu: indexed %lu (vertices to %lu, base %ld), mode 0x%x, program %u, inputs 0x%lx",
				gl.draws, count, vertex_end, base_vertex, mode, gl.program, vertex->inputs);
		IDirect3DDevice9_DrawIndexedVertices(gl.device, type, 0, 0, (UINT)count);
	}
	else
	{
		if (!setup_vertices(vertex, first, vertex_end))
			return;
		if (draw_logged())
			x360_log("gl: draw %lu: %lu from %lu, mode 0x%x, program %u, inputs 0x%lx", gl.draws, count, first, mode,
				gl.program, vertex->inputs);
		IDirect3DDevice9_DrawVertices(gl.device, type, (UINT)first, (UINT)count);
	}
	gl.draws++;
	targets_written();
}

/* ---------- blits: a textured quad */

static void blit_quad(struct texture *source, float u0, float v0, float u1, float v1, float x0, float y0, float x1,
	float y1, unsigned long width, unsigned long height, BOOL linear)
{
	float vertices[4][6];
	int corner;
	D3DVIEWPORT9 viewport;

	if (!gl.blit_vertex || !gl.blit_pixel || !source->d3d)
		return;
	for (corner = 0; corner < 4; corner++)
	{
		float x = (corner & 1) ? x1 : x0, y = (corner & 2) ? y1 : y0;

		vertices[corner][0] = x / (float)width * 2.0f - 1.0f;
		vertices[corner][1] = 1.0f - y / (float)height * 2.0f;
		vertices[corner][2] = 0.0f;
		vertices[corner][3] = 1.0f;
		vertices[corner][4] = (corner & 1) ? u1 : u0;
		vertices[corner][5] = (corner & 2) ? v1 : v0;
	}
	viewport.X = 0;
	viewport.Y = 0;
	viewport.Width = width;
	viewport.Height = height;
	viewport.MinZ = 0.0f;
	viewport.MaxZ = 1.0f;
	IDirect3DDevice9_SetViewport(gl.device, &viewport);
	IDirect3DDevice9_SetRenderState(gl.device, D3DRS_HALFPIXELOFFSET, TRUE);
	IDirect3DDevice9_SetRenderState(gl.device, D3DRS_ALPHABLENDENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(gl.device, D3DRS_ZENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(gl.device, D3DRS_ZWRITEENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(gl.device, D3DRS_STENCILENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(gl.device, D3DRS_CULLMODE, D3DCULL_NONE);
	IDirect3DDevice9_SetRenderState(gl.device, D3DRS_SCISSORTESTENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(gl.device, D3DRS_FILLMODE, D3DFILL_SOLID);
	IDirect3DDevice9_SetRenderState(gl.device, D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALL);
	IDirect3DDevice9_SetVertexDeclaration(gl.device, gl.blit_declaration);
	IDirect3DDevice9_SetVertexShader(gl.device, gl.blit_vertex);
	IDirect3DDevice9_SetPixelShader(gl.device, gl.blit_pixel);
	IDirect3DDevice9_SetTexture(gl.device, 0, source->d3d);
	IDirect3DDevice9_SetSamplerState(gl.device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	IDirect3DDevice9_SetSamplerState(gl.device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	IDirect3DDevice9_SetSamplerState(gl.device, 0, D3DSAMP_MAGFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
	IDirect3DDevice9_SetSamplerState(gl.device, 0, D3DSAMP_MINFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
	IDirect3DDevice9_SetSamplerState(gl.device, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	IDirect3DDevice9_SetSamplerState(gl.device, 0, D3DSAMP_MINMIPLEVEL, 0);
	IDirect3DDevice9_DrawVerticesUP(gl.device, D3DPT_TRIANGLESTRIP, 4, vertices, sizeof(vertices[0]));
}

/* ---------- the entry points: state */

static void APIENTRY x360_glEnable(GLenum cap)
{
	switch (cap)
	{
	case GL_BLEND: gl.blend = TRUE; break;
	case GL_DEPTH_TEST: gl.depth_test = TRUE; break;
	case GL_STENCIL_TEST: gl.stencil_test = TRUE; break;
	case GL_CULL_FACE: gl.cull_face = TRUE; break;
	case GL_SCISSOR_TEST: gl.scissor_test = TRUE; break;
	case GL_POLYGON_OFFSET_FILL: gl.polygon_offset_fill = TRUE; break;
	default: break;
	}
}

static void APIENTRY x360_glDisable(GLenum cap)
{
	switch (cap)
	{
	case GL_BLEND: gl.blend = FALSE; break;
	case GL_DEPTH_TEST: gl.depth_test = FALSE; break;
	case GL_STENCIL_TEST: gl.stencil_test = FALSE; break;
	case GL_CULL_FACE: gl.cull_face = FALSE; break;
	case GL_SCISSOR_TEST: gl.scissor_test = FALSE; break;
	case GL_POLYGON_OFFSET_FILL: gl.polygon_offset_fill = FALSE; break;
	default: break;
	}
}

static GLboolean APIENTRY x360_glIsEnabled(GLenum cap)
{
	switch (cap)
	{
	case GL_BLEND: return (GLboolean)gl.blend;
	case GL_DEPTH_TEST: return (GLboolean)gl.depth_test;
	case GL_STENCIL_TEST: return (GLboolean)gl.stencil_test;
	case GL_CULL_FACE: return (GLboolean)gl.cull_face;
	case GL_SCISSOR_TEST: return (GLboolean)gl.scissor_test;
	case GL_POLYGON_OFFSET_FILL: return (GLboolean)gl.polygon_offset_fill;
	default: return GL_FALSE;
	}
}

static void APIENTRY x360_glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	gl.blend_color[0] = r; gl.blend_color[1] = g; gl.blend_color[2] = b; gl.blend_color[3] = a;
}
static void APIENTRY x360_glBlendEquation(GLenum mode) { gl.blend_equation = mode; }
static void APIENTRY x360_glBlendFunc(GLenum source, GLenum destination)
{
	gl.blend_source = gl.blend_source_alpha = source;
	gl.blend_destination = gl.blend_destination_alpha = destination;
}
static void APIENTRY x360_glBlendFuncSeparate(GLenum source, GLenum destination, GLenum source_alpha,
	GLenum destination_alpha)
{
	gl.blend_source = source;
	gl.blend_destination = destination;
	gl.blend_source_alpha = source_alpha;
	gl.blend_destination_alpha = destination_alpha;
}
static void APIENTRY x360_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
	gl.clear_color[0] = r; gl.clear_color[1] = g; gl.clear_color[2] = b; gl.clear_color[3] = a;
}
static void APIENTRY x360_glClearDepth(GLclampd depth) { gl.clear_depth = depth; }
static void APIENTRY x360_glClearStencil(GLint s) { gl.clear_stencil = s; }
static void APIENTRY x360_glClipControl(GLenum origin, GLenum depth) { (void)origin; (void)depth; }
static void APIENTRY x360_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
	gl.color_mask[0] = r; gl.color_mask[1] = g; gl.color_mask[2] = b; gl.color_mask[3] = a;
}
static void APIENTRY x360_glCullFace(GLenum mode) { gl.cull_mode = mode; }
static void APIENTRY x360_glFrontFace(GLenum mode) { gl.front_face = mode; }
static void APIENTRY x360_glDepthFunc(GLenum func) { gl.depth_func = func; }
static void APIENTRY x360_glDepthMask(GLboolean flag) { gl.depth_mask = flag; }
static void APIENTRY x360_glDepthRange(GLclampd near_value, GLclampd far_value)
{
	gl.depth_near = near_value;
	gl.depth_far = far_value;
}
static void APIENTRY x360_glStencilFunc(GLenum func, GLint reference, GLuint mask)
{
	gl.stencil_func = func;
	gl.stencil_reference = reference;
	gl.stencil_mask = mask;
}
static void APIENTRY x360_glStencilMask(GLuint mask) { gl.stencil_write_mask = mask; }
static void APIENTRY x360_glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	gl.stencil_fail = fail;
	gl.stencil_zfail = zfail;
	gl.stencil_zpass = zpass;
}
static void APIENTRY x360_glLineWidth(GLfloat width) { (void)width; }
static void APIENTRY x360_glPolygonMode(GLenum face, GLenum mode) { (void)face; gl.polygon_mode = mode; }
static void APIENTRY x360_glPolygonOffset(GLfloat factor, GLfloat units)
{
	gl.polygon_offset_factor = factor;
	gl.polygon_offset_units = units;
}
static void APIENTRY x360_glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	gl.scissor[0] = x; gl.scissor[1] = y; gl.scissor[2] = width; gl.scissor[3] = height;
}
static void APIENTRY x360_glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	gl.viewport[0] = x; gl.viewport[1] = y; gl.viewport[2] = width; gl.viewport[3] = height;
}
static void APIENTRY x360_glPixelStorei(GLenum name, GLint value) { (void)name; (void)value; }
static void APIENTRY x360_glFinish(void) { if (gl.ready) IDirect3DDevice9_BlockUntilIdle(gl.device); }
static void APIENTRY x360_glFlush(void) { }
static void APIENTRY x360_glMemoryBarrier(GLbitfield barriers) { (void)barriers; }
static void APIENTRY x360_glDebugMessageCallback(GLDEBUGPROC callback, const void *user) { (void)callback; (void)user; }
static GLenum APIENTRY x360_glGetError(void) { return GL_NO_ERROR; }

static void APIENTRY x360_glGetIntegerv(GLenum name, GLint *value)
{
	switch (name)
	{
	case GL_MAJOR_VERSION: *value = 4; break;
	case GL_MINOR_VERSION: *value = 5; break;
	case GL_MAX_SAMPLES: *value = 1; break;
	case GL_MAX_TEXTURE_SIZE: *value = 4096; break;
	case GL_MAX_RENDERBUFFER_SIZE: *value = 4096; break;
	case GL_VIEWPORT: memcpy(value, gl.viewport, sizeof(gl.viewport)); break;
	case GL_SCISSOR_BOX: memcpy(value, gl.scissor, sizeof(gl.scissor)); break;
	case GL_DRAW_FRAMEBUFFER_BINDING: *value = (GLint)gl.draw_framebuffer; break;
	case GL_READ_FRAMEBUFFER_BINDING: *value = (GLint)gl.read_framebuffer; break;
	case GL_CURRENT_PROGRAM: *value = (GLint)gl.program; break;
	case GL_ACTIVE_TEXTURE: *value = (GLint)(GL_TEXTURE0 + gl.active_unit); break;
	case GL_TEXTURE_BINDING_2D: *value = (GLint)*bound_texture_slot(GL_TEXTURE_2D); break;
	case GL_ARRAY_BUFFER_BINDING: *value = (GLint)gl.array_buffer; break;
	case GL_ELEMENT_ARRAY_BUFFER_BINDING: *value = (GLint)gl.element_buffer; break;
	default: *value = 0; break;
	}
}

static const GLubyte *APIENTRY x360_glGetString(GLenum name)
{
	switch (name)
	{
	case GL_VENDOR: return (const GLubyte *)"ChupathingyCE";
	case GL_RENDERER: return (const GLubyte *)"Xbox 360 Xenos (Direct3D 9)";
	case GL_VERSION: return (const GLubyte *)"4.5 (on Direct3D 9)";
	case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte *)"4.50 (to HLSL)";
	default: return (const GLubyte *)"";
	}
}

/* ---------- the entry points: textures and samplers */

static void APIENTRY x360_glGenTextures(GLsizei n, GLuint *names)
{
	GLsizei index;

	generate(n, names, _object_texture, sizeof(struct texture));
	for (index = 0; index < n; index++)
	{
		struct texture *texture = object_get(names[index], _object_texture);

		if (texture)
			texture_defaults(texture);
	}
}

static void APIENTRY x360_glDeleteTextures(GLsizei n, const GLuint *names)
{
	GLsizei index;

	for (index = 0; index < n; index++)
	{
		struct texture *texture = object_get(names[index], _object_texture);

		if (!texture)
			continue;
		/* (not while the GPU may read it: released two frames on) */
		texture_retire(texture);
		stats.textures_deleted++;
		objects[names[index]].type = _object_none;
		objects[names[index]].data = NULL;
	}
}

static void APIENTRY x360_glActiveTexture(GLenum unit) { gl.active_unit = unit - GL_TEXTURE0; }
static void APIENTRY x360_glBindTexture(GLenum target, GLuint name) { *bound_texture_slot(target) = name; }

static void APIENTRY x360_glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height,
	GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	(void)border;
	image(target, level, internal, width, height, 1, format, type, pixels, FALSE);
}

static void APIENTRY x360_glTexImage3D(GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height,
	GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels)
{
	(void)border;
	image(target, level, internal, width, height, depth, format, type, pixels, FALSE);
}

static void APIENTRY x360_glCompressedTexImage2D(GLenum target, GLint level, GLenum format, GLsizei width,
	GLsizei height, GLint border, GLsizei size, const void *data)
{
	(void)border;
	(void)size;
	image(target, level, (GLint)format, width, height, 1, 0, 0, data, TRUE);
}

static void APIENTRY x360_glCompressedTexImage3D(GLenum target, GLint level, GLenum format, GLsizei width,
	GLsizei height, GLsizei depth, GLint border, GLsizei size, const void *data)
{
	(void)border;
	(void)size;
	image(target, level, (GLint)format, width, height, depth, 0, 0, data, TRUE);
}

static void APIENTRY x360_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width,
	GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	struct texture *texture = bound_texture(target);

	(void)type;
	if (texture)
		texture_upload(texture, target, level, (unsigned long)x, (unsigned long)y, (unsigned long)width,
			(unsigned long)height, 1, pixels, format);
}

static void texture_parameter(struct texture *texture, struct sampler *sampler, GLenum name, const GLint *integer,
	const GLfloat *real)
{
	GLint value = integer ? *integer : (GLint)*real;
	GLfloat value_float = real ? *real : (GLfloat)*integer;

	switch (name)
	{
	case GL_TEXTURE_MIN_FILTER: if (texture) texture->min_filter = value; if (sampler) sampler->min_filter = value; break;
	case GL_TEXTURE_MAG_FILTER: if (texture) texture->mag_filter = value; if (sampler) sampler->mag_filter = value; break;
	case GL_TEXTURE_WRAP_S: if (texture) texture->wrap_s = value; if (sampler) sampler->wrap_s = value; break;
	case GL_TEXTURE_WRAP_T: if (texture) texture->wrap_t = value; if (sampler) sampler->wrap_t = value; break;
	case GL_TEXTURE_WRAP_R: if (texture) texture->wrap_r = value; if (sampler) sampler->wrap_r = value; break;
	case GL_TEXTURE_BASE_LEVEL: if (texture) texture->base_level = value; break;
	case GL_TEXTURE_MAX_LEVEL: if (texture) texture->max_level = value; break;
	case GL_TEXTURE_LOD_BIAS: if (texture) texture->lod_bias = value_float; if (sampler) sampler->lod_bias = value_float; break;
	case GL_TEXTURE_MAX_ANISOTROPY_EXT:
		if (texture) texture->anisotropy = value_float;
		if (sampler) sampler->anisotropy = value_float;
		break;
	case GL_TEXTURE_BORDER_COLOR:
		if (real)
		{
			if (texture) memcpy(texture->border, real, sizeof(texture->border));
			if (sampler) memcpy(sampler->border, real, sizeof(sampler->border));
		}
		break;
	default: break;
	}
}

static void APIENTRY x360_glTexParameteri(GLenum target, GLenum name, GLint value)
{
	texture_parameter(bound_texture(target), NULL, name, &value, NULL);
}
static void APIENTRY x360_glTexParameteriv(GLenum target, GLenum name, const GLint *value)
{
	texture_parameter(bound_texture(target), NULL, name, value, NULL);
}
static void APIENTRY x360_glTexParameterf(GLenum target, GLenum name, GLfloat value)
{
	texture_parameter(bound_texture(target), NULL, name, NULL, &value);
}
static void APIENTRY x360_glTexParameterfv(GLenum target, GLenum name, const GLfloat *value)
{
	texture_parameter(bound_texture(target), NULL, name, NULL, value);
}

static void APIENTRY x360_glGenSamplers(GLsizei n, GLuint *names)
{
	GLsizei index;

	generate(n, names, _object_sampler, sizeof(struct sampler));
	for (index = 0; index < n; index++)
	{
		struct sampler *sampler = object_get(names[index], _object_sampler);

		if (sampler)
		{
			sampler->min_filter = GL_NEAREST_MIPMAP_LINEAR;
			sampler->mag_filter = GL_LINEAR;
			sampler->wrap_s = sampler->wrap_t = sampler->wrap_r = GL_REPEAT;
			sampler->anisotropy = 1.0f;
		}
	}
}
static void APIENTRY x360_glBindSampler(GLuint unit, GLuint sampler)
{
	if (unit < TEXTURE_UNITS)
		gl.samplers[unit] = sampler;
}
static void APIENTRY x360_glSamplerParameteri(GLuint sampler, GLenum name, GLint value)
{
	texture_parameter(NULL, object_get(sampler, _object_sampler), name, &value, NULL);
}
static void APIENTRY x360_glSamplerParameterf(GLuint sampler, GLenum name, GLfloat value)
{
	texture_parameter(NULL, object_get(sampler, _object_sampler), name, NULL, &value);
}
static void APIENTRY x360_glSamplerParameterfv(GLuint sampler, GLenum name, const GLfloat *value)
{
	texture_parameter(NULL, object_get(sampler, _object_sampler), name, NULL, value);
}

static void APIENTRY x360_glGenerateMipmap(GLenum target)
{
	struct texture *texture = bound_texture(target);

	if (texture)
		generate_mipmaps(texture);
}

static void APIENTRY x360_glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
	FIRST_USE();
	(void)target; (void)level; (void)format; (void)type; (void)pixels;
}

static void APIENTRY x360_glCopyImageSubData(GLuint a, GLenum b, GLint c, GLint d, GLint e, GLint f, GLuint g,
	GLenum h, GLint i, GLint j, GLint k, GLint l, GLsizei m, GLsizei n, GLsizei o)
{
	FIRST_USE();
	(void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k; (void)l;
	(void)m; (void)n; (void)o;
}

/* ---------- the entry points: framebuffers */

static void APIENTRY x360_glGenFramebuffers(GLsizei n, GLuint *names)
{
	generate(n, names, _object_framebuffer, sizeof(struct framebuffer));
}

static void APIENTRY x360_glDeleteFramebuffers(GLsizei n, const GLuint *names)
{
	GLsizei index;

	for (index = 0; index < n; index++)
	{
		void *framebuffer = object_get(names[index], _object_framebuffer);

		if (!framebuffer)
			continue;
		free(framebuffer);
		objects[names[index]].type = _object_none;
		if (gl.draw_framebuffer == names[index])
			gl.draw_framebuffer = 0;
		if (gl.read_framebuffer == names[index])
			gl.read_framebuffer = 0;
	}
}

static void APIENTRY x360_glBindFramebuffer(GLenum target, GLuint framebuffer)
{
	if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
		gl.draw_framebuffer = framebuffer;
	if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
		gl.read_framebuffer = framebuffer;
}

static void APIENTRY x360_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum texture_target,
	GLuint texture, GLint level)
{
	struct framebuffer *framebuffer = object_get(target == GL_READ_FRAMEBUFFER ? gl.read_framebuffer :
		gl.draw_framebuffer, _object_framebuffer);

	(void)texture_target;
	if (!framebuffer)
		return;
	if (attachment == GL_COLOR_ATTACHMENT0)
	{
		framebuffer->color = texture;
		framebuffer->color_level = level;
	}
	else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT || attachment == GL_DEPTH_ATTACHMENT)
	{
		framebuffer->depth = texture;
	}
	/* (the bound surfaces are looked at again at the next draw) */
	gl.bound_color = (D3DSurface *)1;
}

static void APIENTRY x360_glDrawBuffers(GLsizei n, const GLenum *buffers) { (void)n; (void)buffers; }
static GLenum APIENTRY x360_glCheckFramebufferStatus(GLenum target) { (void)target; return GL_FRAMEBUFFER_COMPLETE; }

static void APIENTRY x360_glGenRenderbuffers(GLsizei n, GLuint *names)
{
	generate(n, names, _object_renderbuffer, sizeof(struct renderbuffer));
}
static void APIENTRY x360_glBindRenderbuffer(GLenum target, GLuint renderbuffer)
{
	(void)target;
	gl.renderbuffer = renderbuffer;
}
static void APIENTRY x360_glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum format,
	GLsizei width, GLsizei height)
{
	FIRST_USE();
	(void)target; (void)samples; (void)format; (void)width; (void)height;
}
static void APIENTRY x360_glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffer_target,
	GLuint renderbuffer)
{
	FIRST_USE();
	(void)target; (void)attachment; (void)renderbuffer_target; (void)renderbuffer;
}

static void APIENTRY x360_glClear(GLbitfield mask)
{
	DWORD flags = 0;
	D3DVECTOR4 color;
	D3DRECT rectangle;

	if (!gl_ready())
		return;
	bind_targets();
	if ((mask & GL_COLOR_BUFFER_BIT) && (gl.bound_color || gl.bound_screen))
		flags |= D3DCLEAR_TARGET;
	if ((mask & GL_DEPTH_BUFFER_BIT) && gl.bound_depth)
		flags |= D3DCLEAR_ZBUFFER;
	if ((mask & GL_STENCIL_BUFFER_BIT) && gl.bound_depth)
		flags |= D3DCLEAR_STENCIL;
	if (!flags)
		return;
	if (draw_logged())
	{
		x360_log("gl: clear 0x%lx (color %.2f %.2f %.2f %.2f, depth %.2f), scissor %d (%d %d %d %d), framebuffer %u",
			(unsigned long)flags, gl.clear_color[0], gl.clear_color[1], gl.clear_color[2], gl.clear_color[3],
			gl.clear_depth, gl.scissor_test, gl.scissor[0], gl.scissor[1], gl.scissor[2], gl.scissor[3],
			gl.draw_framebuffer);
	}
	color.x = gl.clear_color[0];
	color.y = gl.clear_color[1];
	color.z = gl.clear_color[2];
	color.w = gl.clear_color[3];
	debug_load();
	if (debug.clear_red && gl.draw_framebuffer)
	{
		color.x = 1.0f;
		color.y = 0.0f;
		color.z = 0.0f;
		color.w = 1.0f;
	}
	if (gl.scissor_test)
	{
		rectangle.x1 = gl.scissor[0];
		rectangle.y1 = gl.scissor[1];
		rectangle.x2 = gl.scissor[0] + gl.scissor[2];
		rectangle.y2 = gl.scissor[1] + gl.scissor[3];
	}
	IDirect3DDevice9_SetRenderState(gl.device, D3DRS_SCISSORTESTENABLE, FALSE);
	IDirect3DDevice9_ClearF(gl.device, flags, gl.scissor_test ? &rectangle : NULL, &color, (float)gl.clear_depth,
		(DWORD)gl.clear_stencil);
	if (flags & D3DCLEAR_TARGET)
		targets_written();
}

/* the read framebuffer's color, or the screen's for 0 (presenting) */
static void APIENTRY x360_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0,
	GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
{
	struct framebuffer *read = object_get(gl.read_framebuffer, _object_framebuffer);
	struct texture *source = read ? object_get(read->color, _object_texture) : NULL;
	unsigned long width, height;
	float u0, v0, u1, v1;

	if (!(mask & GL_COLOR_BUFFER_BIT) || !source || !gl_ready())
	{
		FIRST_USE();
		return;
	}
	texture_resolve(source);
	bind_targets();
	if (!gl.draw_framebuffer)
	{
		/* the screen: GL counts its rows from the bottom */
		D3DSurface *back_buffer = NULL;
		D3DSURFACE_DESC description;

		IDirect3DDevice9_GetBackBuffer(gl.device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer);
		D3DSurface_GetDesc(back_buffer, &description);
		width = description.Width;
		height = description.Height;
		if (back_buffer)
			IDirect3DResource9_Release((D3DResource *)back_buffer);
		dy0 = (GLint)height - dy0;
		dy1 = (GLint)height - dy1;
	}
	else
	{
		struct framebuffer *draw = object_get(gl.draw_framebuffer, _object_framebuffer);
		struct texture *destination = draw ? object_get(draw->color, _object_texture) : NULL;

		if (!destination)
			return;
		width = destination->width;
		height = destination->height;
	}
	u0 = (float)sx0 / (float)source->width;
	u1 = (float)sx1 / (float)source->width;
	v0 = (float)sy0 / (float)source->height;
	v1 = (float)sy1 / (float)source->height;
	blit_quad(source, u0, v0, u1, v1, (float)dx0, (float)dy0, (float)dx1, (float)dy1, width, height,
		filter == GL_LINEAR);
	if (!gl.draw_framebuffer)
		gl.presented = source;
	targets_written();
}

static void APIENTRY x360_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
	GLvoid *pixels)
{
	FIRST_USE();
	(void)x; (void)y; (void)width; (void)height; (void)format; (void)type;
	if (pixels && width > 0 && height > 0)
		memset(pixels, 0, (size_t)width * (size_t)height * 4);
}

/* ---------- the entry points: buffers */

static struct buffer *bound_buffer(GLenum target)
{
	GLuint name = target == GL_ELEMENT_ARRAY_BUFFER ? gl.element_buffer : target == GL_ARRAY_BUFFER ?
		gl.array_buffer : target == GL_COPY_WRITE_BUFFER ? gl.copy_write_buffer : 0;

	return object_get(name, _object_buffer);
}

static void APIENTRY x360_glGenBuffers(GLsizei n, GLuint *names)
{
	generate(n, names, _object_buffer, sizeof(struct buffer));
}

static void APIENTRY x360_glDeleteBuffers(GLsizei n, const GLuint *names)
{
	GLsizei index;

	for (index = 0; index < n; index++)
	{
		struct buffer *buffer = object_get(names[index], _object_buffer);

		if (!buffer)
			continue;
		if (!buffer->alias)
			storage_retire(buffer->data);
		free(buffer);
		objects[names[index]].type = _object_none;
	}
}

static void APIENTRY x360_glBindBuffer(GLenum target, GLuint buffer)
{
	if (target == GL_ARRAY_BUFFER)
		gl.array_buffer = buffer;
	else if (target == GL_ELEMENT_ARRAY_BUFFER)
		gl.element_buffer = buffer;
	else if (target == GL_COPY_WRITE_BUFFER)
		gl.copy_write_buffer = buffer;
}

static void APIENTRY x360_glBindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
	FIRST_USE();
	(void)target; (void)index; (void)buffer;
}

/* new storage each time (what the GPU may still read of the old is kept
until it is done: a stream buffer orphaned when full) */
static void APIENTRY x360_glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
	struct buffer *buffer = bound_buffer(target);
	unsigned char *storage;

	(void)usage;
	if (!buffer)
		return;
	storage = storage_allocate((unsigned long)size);
	if (!storage)
	{
		x360_log("gl: out of memory for a %lu byte buffer", (unsigned long)size);
		return;
	}
	if (!buffer->alias)
		storage_retire(buffer->data);
	buffer->data = storage;
	buffer->size = (unsigned long)size;
	buffer->alias = FALSE;
	if (data)
	{
		memcpy(buffer->data, data, (size_t)size);
		storage_flush(buffer->data, (unsigned long)size);
	}
}

static void APIENTRY x360_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
	struct buffer *buffer = bound_buffer(target);

	if (!buffer || !buffer->data || (unsigned long)offset + (unsigned long)size > buffer->size)
		return;
	/* (an alias given its own bytes: they are there already) */
	if (data != buffer->data + offset)
		memcpy(buffer->data + offset, data, (size_t)size);
	storage_flush(buffer->data + offset, (unsigned long)size);
}

static struct
{
	struct buffer *buffer;
	unsigned long offset, length;
} mapped;

static void *APIENTRY x360_glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
	struct buffer *buffer = bound_buffer(target);

	(void)access;
	if (!buffer || !buffer->data)
		return NULL;
	mapped.buffer = buffer;
	mapped.offset = (unsigned long)offset;
	mapped.length = (unsigned long)length;
	return buffer->data + offset;
}

static GLboolean APIENTRY x360_glUnmapBuffer(GLenum target)
{
	(void)target;
	if (mapped.buffer && mapped.buffer->data)
		storage_flush(mapped.buffer->data + mapped.offset, mapped.length);
	mapped.buffer = NULL;
	return GL_TRUE;
}

/* the buffer bound to target made an alias of memory the GPU can read in
place (the Xbox window's: d3d8_gl.c's mirror) */
void x360_gl_buffer_alias(GLenum target, void *memory, unsigned long size)
{
	struct buffer *buffer = bound_buffer(target);

	if (!buffer)
		return;
	if (!buffer->alias)
		storage_retire(buffer->data);
	buffer->data = memory;
	buffer->size = size;
	buffer->alias = TRUE;
}

/* ---------- the entry points: vertex attributes */

static void APIENTRY x360_glGenVertexArrays(GLsizei n, GLuint *names)
{
	generate(n, names, _object_vertex_array, 0);
}
static void APIENTRY x360_glBindVertexArray(GLuint array) { (void)array; }
static void APIENTRY x360_glEnableVertexAttribArray(GLuint index)
{
	if (index < ATTRIBUTES)
		gl.attributes[index].enabled = TRUE;
}
static void APIENTRY x360_glDisableVertexAttribArray(GLuint index)
{
	if (index < ATTRIBUTES)
		gl.attributes[index].enabled = FALSE;
}

static void attribute_format(GLuint index, GLint size, GLenum type, GLboolean normalized, BOOL integer,
	GLuint offset)
{
	struct attribute *attribute;

	if (index >= ATTRIBUTES)
		return;
	attribute = &gl.attributes[index];
	attribute->size = size;
	attribute->type = type;
	attribute->normalized = normalized;
	attribute->integer = integer;
	attribute->relative_offset = offset;
}

static void APIENTRY x360_glVertexAttribFormat(GLuint index, GLint size, GLenum type, GLboolean normalized,
	GLuint offset)
{
	attribute_format(index, size, type, normalized, FALSE, offset);
}
static void APIENTRY x360_glVertexAttribIFormat(GLuint index, GLint size, GLenum type, GLuint offset)
{
	attribute_format(index, size, type, GL_FALSE, TRUE, offset);
}
static void APIENTRY x360_glVertexAttribBinding(GLuint index, GLuint binding)
{
	if (index < ATTRIBUTES)
		gl.attributes[index].binding = binding;
}
static void APIENTRY x360_glBindVertexBuffer(GLuint binding, GLuint buffer, GLintptr offset, GLsizei stride)
{
	if (binding >= BINDINGS)
		return;
	gl.bindings[binding].buffer = buffer;
	gl.bindings[binding].offset = offset;
	gl.bindings[binding].stride = stride;
}

/* (the older form: a binding of the attribute's own) */
static void attribute_pointer(GLuint index, GLint size, GLenum type, GLboolean normalized, BOOL integer,
	GLsizei stride, const void *pointer)
{
	unsigned long bytes;

	if (index >= ATTRIBUTES)
		return;
	attribute_format(index, size, type, normalized, integer, 0);
	gl.attributes[index].binding = index;
	bytes = attribute_bytes(&gl.attributes[index], FALSE);
	x360_glBindVertexBuffer(index, gl.array_buffer, (GLintptr)pointer, stride ? stride : (GLsizei)bytes);
}

static void APIENTRY x360_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
	GLsizei stride, const void *pointer)
{
	attribute_pointer(index, size, type, normalized, FALSE, stride, pointer);
}
static void APIENTRY x360_glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride,
	const void *pointer)
{
	attribute_pointer(index, size, type, GL_FALSE, TRUE, stride, pointer);
}
static void APIENTRY x360_glVertexAttrib4fv(GLuint index, const GLfloat *v)
{
	if (index < ATTRIBUTES)
		memcpy(gl.attributes[index].value, v, sizeof(gl.attributes[index].value));
}
static void APIENTRY x360_glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w)
{
	if (index >= ATTRIBUTES)
		return;
	gl.attributes[index].value[0] = (float)x;
	gl.attributes[index].value[1] = (float)y;
	gl.attributes[index].value[2] = (float)z;
	gl.attributes[index].value[3] = (float)w;
}

/* ---------- the entry points: draws */

static void APIENTRY x360_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	draw(mode, (unsigned long)first, (unsigned long)count, 0, NULL, 0);
}
static void APIENTRY x360_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	draw(mode, 0, (unsigned long)count, type, indices, 0);
}
static void APIENTRY x360_glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices,
	GLint base)
{
	draw(mode, 0, (unsigned long)count, type, indices, base);
}

/* ---------- the entry points: shaders and programs */

static GLuint APIENTRY x360_glCreateShader(GLenum type)
{
	struct shader *shader = calloc(1, sizeof(*shader));
	GLuint name;

	if (!shader)
		return 0;
	shader->type = type;
	name = object_new(_object_shader, shader);
	if (!name)
		free(shader);
	return name;
}

static void APIENTRY x360_glShaderSource(GLuint name, GLsizei count, const GLchar *const *strings,
	const GLint *lengths)
{
	struct shader *shader = object_get(name, _object_shader);
	size_t total = 0;
	GLsizei index;

	if (!shader)
		return;
	for (index = 0; index < count; index++)
		total += lengths && lengths[index] >= 0 ? (size_t)lengths[index] : strlen(strings[index]);
	free(shader->source);
	shader->source = malloc(total + 1);
	if (!shader->source)
		return;
	total = 0;
	for (index = 0; index < count; index++)
	{
		size_t length = lengths && lengths[index] >= 0 ? (size_t)lengths[index] : strlen(strings[index]);

		memcpy(shader->source + total, strings[index], length);
		total += length;
	}
	shader->source[total] = '\0';
	shader->compiled = FALSE;
}

static void APIENTRY x360_glCompileShader(GLuint name)
{
	struct shader *shader = object_get(name, _object_shader);

	if (shader && gl_ready())
		shader_compile(shader);
}

static void APIENTRY x360_glGetShaderiv(GLuint name, GLenum parameter, GLint *value)
{
	struct shader *shader = object_get(name, _object_shader);

	switch (parameter)
	{
	case GL_COMPILE_STATUS: *value = shader && (shader->vertex || shader->pixel) ? GL_TRUE : GL_FALSE; break;
	case GL_INFO_LOG_LENGTH: *value = 1; break;
	default: *value = 0; break;
	}
}

static void APIENTRY x360_glGetShaderInfoLog(GLuint shader, GLsizei size, GLsizei *length, GLchar *log)
{
	(void)shader;
	if (size > 0)
		log[0] = '\0';
	if (length)
		*length = 0;
}

static void APIENTRY x360_glDeleteShader(GLuint name)
{
	(void)name;
	/* (kept: a program made from it still uses its compiled code) */
}

static GLuint APIENTRY x360_glCreateProgram(void)
{
	struct program *program = calloc(1, sizeof(*program));
	GLuint name;

	if (!program)
		return 0;
	name = object_new(_object_program, program);
	if (!name)
		free(program);
	return name;
}

static void APIENTRY x360_glAttachShader(GLuint program_name, GLuint shader_name)
{
	struct program *program = object_get(program_name, _object_program);
	struct shader *shader = object_get(shader_name, _object_shader);

	if (!program || !shader)
		return;
	if (shader->type == GL_VERTEX_SHADER)
		program->vertex = shader_name;
	else
		program->fragment = shader_name;
}

static void APIENTRY x360_glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
	(void)program; (void)index; (void)name;
}
static void APIENTRY x360_glBindFragDataLocation(GLuint program, GLuint color, const GLchar *name)
{
	(void)program; (void)color; (void)name;
}

static void APIENTRY x360_glLinkProgram(GLuint name)
{
	struct program *program = object_get(name, _object_program);
	struct shader *vertex, *fragment;

	if (!program || !gl_ready())
		return;
	vertex = object_get(program->vertex, _object_shader);
	fragment = object_get(program->fragment, _object_shader);
	program->linked = vertex && fragment && shader_compile(vertex) && shader_compile(fragment);
	if (program->linked)
		program_uniforms(program, vertex, fragment);
}

static void APIENTRY x360_glGetProgramiv(GLuint name, GLenum parameter, GLint *value)
{
	struct program *program = object_get(name, _object_program);

	switch (parameter)
	{
	case GL_LINK_STATUS: *value = program && program->linked ? GL_TRUE : GL_FALSE; break;
	case GL_INFO_LOG_LENGTH: *value = 1; break;
	default: *value = 0; break;
	}
}

static void APIENTRY x360_glGetProgramInfoLog(GLuint program, GLsizei size, GLsizei *length, GLchar *log)
{
	(void)program;
	if (size > 0)
		log[0] = '\0';
	if (length)
		*length = 0;
}

static void APIENTRY x360_glUseProgram(GLuint program) { gl.program = program; }

static GLint APIENTRY x360_glGetUniformLocation(GLuint name, const GLchar *uniform_name)
{
	struct program *program = object_get(name, _object_program);
	char base[32];
	const char *bracket = strchr(uniform_name, '[');
	size_t length = bracket ? (size_t)(bracket - uniform_name) : strlen(uniform_name);
	int element = bracket ? atoi(bracket + 1) : 0;
	int index;

	if (!program || length >= sizeof(base))
		return -1;
	memcpy(base, uniform_name, length);
	base[length] = '\0';
	for (index = 0; index < program->uniform_count; index++)
	{
		struct uniform *uniform = &program->uniforms[index];

		if (strcmp(uniform->name, base))
			continue;
		if (!uniform->sampler && element >= uniform->count)
			return -1;
		return LOCATION(index, element);
	}
	return -1;
}

static void APIENTRY x360_glUniform1i(GLint location, GLint v0)
{
	struct program *program = object_get(gl.program, _object_program);
	int index = location >= 0 ? LOCATION_UNIFORM(location) : -1;

	if (!program || index < 0 || index >= program->uniform_count)
		return;
	if (program->uniforms[index].sampler)
		program->uniforms[index].unit = v0;
	else
	{
		float value = (float)v0;

		uniform_set(location, &value, 1, 1);
	}
}
static void APIENTRY x360_glUniform1iv(GLint location, GLsizei count, const GLint *values)
{
	GLsizei index;

	for (index = 0; index < count; index++)
		x360_glUniform1i(location + index, values[index]);
}
static void APIENTRY x360_glUniform1f(GLint location, GLfloat v0) { uniform_set(location, &v0, 1, 1); }
static void APIENTRY x360_glUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
	float values[2] = { v0, v1 };

	uniform_set(location, values, 1, 2);
}
static void APIENTRY x360_glUniform4fv(GLint location, GLsizei count, const GLfloat *values)
{
	uniform_set(location, values, (int)count, 4);
}

/* ---------- the entry points: queries (occlusion) */

static void APIENTRY x360_glGenQueries(GLsizei n, GLuint *names)
{
	generate(n, names, _object_query, sizeof(struct query));
}

static GLuint active_query;

static void APIENTRY x360_glBeginQuery(GLenum target, GLuint name)
{
	struct query *query = object_get(name, _object_query);

	(void)target;
	if (!query || !gl_ready())
		return;
	if (!query->d3d)
		IDirect3DDevice9_CreateQuery(gl.device, D3DQUERYTYPE_OCCLUSION, &query->d3d);
	if (!query->d3d)
		return;
	bind_targets();
	D3DQuery_Issue(query->d3d, D3DISSUE_BEGIN);
	query->issued = TRUE;
	query->available = FALSE;
	active_query = name;
}

static void APIENTRY x360_glEndQuery(GLenum target)
{
	struct query *query = object_get(active_query, _object_query);

	(void)target;
	if (query && query->d3d && query->issued)
		D3DQuery_Issue(query->d3d, D3DISSUE_END);
	active_query = 0;
}

static void APIENTRY x360_glGetQueryObjectuiv(GLuint name, GLenum parameter, GLuint *value)
{
	struct query *query = object_get(name, _object_query);

	*value = 0;
	if (!query || !query->d3d || !query->issued)
	{
		if (parameter == GL_QUERY_RESULT_AVAILABLE)
			*value = GL_TRUE;
		return;
	}
	if (!query->available)
	{
		DWORD result = 0;

		if (D3DQuery_GetData(query->d3d, &result, sizeof(result),
			parameter == GL_QUERY_RESULT ? D3DGETDATA_FLUSH : 0) == S_OK)
		{
			query->available = TRUE;
			query->result = result;
		}
		else if (parameter == GL_QUERY_RESULT)
		{
			while (D3DQuery_GetData(query->d3d, &result, sizeof(result), D3DGETDATA_FLUSH) != S_OK)
				;
			query->available = TRUE;
			query->result = result;
		}
	}
	if (parameter == GL_QUERY_RESULT_AVAILABLE)
		*value = query->available ? GL_TRUE : GL_FALSE;
	else
		*value = (GLuint)query->result;
}

static void APIENTRY x360_glGetQueryBufferObjectuiv(GLuint id, GLuint buffer, GLenum name, GLintptr offset)
{
	FIRST_USE();
	(void)id; (void)buffer; (void)name; (void)offset;
}

/* ---------- presenting */

/* the frame (sdl_platform.c's platform_video_swap): the screen presented */
void x360_gl_swap(void)
{
	if (!gl_ready())
		return;
	debug_load();
	if (debug.probe && gl.presented && gl.presented->d3d && gl.frame % 150 == 149)
	{
		/* what the game's picture holds: its texels' colors, counted */
		D3DLOCKED_RECT locked;
		unsigned long colors[6] = { 0 }, counts[6] = { 0 }, others = 0, y, x;
		int distinct = 0, index;

		IDirect3DDevice9_BlockUntilIdle(gl.device);
		IDirect3DTexture9_LockRect((D3DTexture *)gl.presented->d3d, 0, &locked, NULL, D3DLOCK_READONLY);
		for (y = 0; y < gl.presented->height; y++)
		{
			const unsigned long *row = (const unsigned long *)((const unsigned char *)locked.pBits + y * locked.Pitch);

			for (x = 0; x < gl.presented->width; x++)
			{
				for (index = 0; index < distinct && colors[index] != row[x]; index++)
					;
				if (index < distinct)
					counts[index]++;
				else if (distinct < 6)
				{
					colors[distinct] = row[x];
					counts[distinct++] = 1;
				}
				else
					others++;
			}
		}
		IDirect3DTexture9_UnlockRect((D3DTexture *)gl.presented->d3d, 0);
		x360_log("gl: picture %lux%lu: %08lx x%lu, %08lx x%lu, %08lx x%lu, %08lx x%lu, %08lx x%lu, %08lx x%lu, "
			"others %lu", gl.presented->width, gl.presented->height, colors[0], counts[0], colors[1], counts[1],
			colors[2], counts[2], colors[3], counts[3], colors[4], counts[4], colors[5], counts[5], others);
	}
	if (debug.dump && gl.presented && gl.presented->d3d && (gl.frame == 300 || gl.frame == 900 || gl.frame == debug.dump_at))
	{
		/* the game's picture, untiled, to game:\picture<n>.raw (BGRA rows) */
		D3DLOCKED_RECT locked;
		unsigned long size = gl.presented->width * gl.presented->height * 4;
		unsigned char *linear = malloc(size);
		char path[64];
		FILE *file;

		IDirect3DDevice9_BlockUntilIdle(gl.device);
		if (linear)
		{
			IDirect3DTexture9_LockRect((D3DTexture *)gl.presented->d3d, 0, &locked, NULL, D3DLOCK_READONLY);
			XGUntileSurface(linear, gl.presented->width * 4, NULL, locked.pBits, gl.presented->width,
				gl.presented->height, NULL, 4);
			IDirect3DTexture9_UnlockRect((D3DTexture *)gl.presented->d3d, 0);
			_snprintf(path, sizeof(path) - 1, "game:\\picture%lu.raw", gl.frame);
			path[sizeof(path) - 1] = '\0';
			file = fopen(path, "wb");
			if (file)
			{
				fwrite(linear, 1, size, file);
				fclose(file);
				x360_log("gl: the picture (%lux%lu) written to %s", gl.presented->width, gl.presented->height, path);
			}
			free(linear);
		}
	}
	D3DDevice_Present(gl.device);
	gl.bound_screen = FALSE;
	gl.bound_color = (D3DSurface *)1;
	rings_next_frame();
	stats.frames++;
	stats.frame_ms += stats_ms_since(stats.frame_start);
	QueryPerformanceCounter(&stats.frame_start);
	if (GetTickCount() - stats.last_report >= 5000)
	{
		if (stats.frames)
		{
			x360_log("gl: %lu frames, %.1f ms a frame; a frame: %lu draws, %lu KB of draw data; waits: %lu idle "
				"(%.1f ms), %lu fence (%.1f ms); %lu textures deleted",
				stats.frames, stats.frame_ms / stats.frames, (gl.draws - stats.draws) / stats.frames,
				stats.ring_bytes / stats.frames / 1024, stats.idle_waits, stats.idle_wait_ms, stats.fence_waits,
				stats.fence_wait_ms, stats.textures_deleted);
		}
		stats.last_report = GetTickCount();
		stats.frames = stats.idle_waits = stats.fence_waits = stats.ring_bytes = stats.textures_deleted = 0;
		stats.idle_wait_ms = stats.fence_wait_ms = stats.frame_ms = 0.0;
		stats.draws = gl.draws;
	}
	gl.frame++;
	if (gl.frame == 1 || gl.frame % 600 == 0)
		x360_log("gl: frame %lu, %lu draws so far", gl.frame, gl.draws);
}

/* ---------- the entry points by name (SDL_GL_GetProcAddress) */

#define ENTRY(name) { #name, (void *)x360_##name },

static const struct
{
	const char *name;
	void *function;
} entries[] =
{
	ENTRY(glActiveTexture) ENTRY(glAttachShader) ENTRY(glBeginQuery) ENTRY(glBindAttribLocation) ENTRY(glBindBuffer)
	ENTRY(glBindBufferBase) ENTRY(glBindFragDataLocation) ENTRY(glBindFramebuffer) ENTRY(glBindRenderbuffer)
	ENTRY(glBindSampler) ENTRY(glBindTexture) ENTRY(glBindVertexArray) ENTRY(glBindVertexBuffer) ENTRY(glBlendColor)
	ENTRY(glBlendEquation) ENTRY(glBlendFunc) ENTRY(glBlendFuncSeparate) ENTRY(glBlitFramebuffer) ENTRY(glBufferData)
	ENTRY(glBufferSubData) ENTRY(glCheckFramebufferStatus) ENTRY(glClear) ENTRY(glClearColor)
	ENTRY(glClearDepth) ENTRY(glClearStencil) ENTRY(glClipControl) ENTRY(glColorMask) ENTRY(glCompileShader)
	ENTRY(glCompressedTexImage2D) ENTRY(glCompressedTexImage3D) ENTRY(glCreateProgram)
	ENTRY(glCreateShader) ENTRY(glCullFace) ENTRY(glDeleteBuffers)
	ENTRY(glDeleteFramebuffers) ENTRY(glDeleteShader) ENTRY(glDeleteTextures) ENTRY(glDepthFunc) ENTRY(glDepthMask)
	ENTRY(glDepthRange) ENTRY(glDisable) ENTRY(glDisableVertexAttribArray) ENTRY(glDrawArrays) ENTRY(glDrawBuffers)
	ENTRY(glDrawElements) ENTRY(glDrawElementsBaseVertex) ENTRY(glEnable) ENTRY(glEnableVertexAttribArray)
	ENTRY(glEndQuery) ENTRY(glFinish) ENTRY(glFlush) ENTRY(glFramebufferRenderbuffer) ENTRY(glFramebufferTexture2D)
	ENTRY(glFrontFace) ENTRY(glGenBuffers) ENTRY(glGenFramebuffers) ENTRY(glGenQueries) ENTRY(glGenRenderbuffers)
	ENTRY(glGenSamplers) ENTRY(glGenTextures) ENTRY(glGenVertexArrays) ENTRY(glGenerateMipmap) ENTRY(glGetError)
	ENTRY(glGetIntegerv) ENTRY(glGetProgramInfoLog) ENTRY(glGetProgramiv) ENTRY(glGetQueryBufferObjectuiv)
	ENTRY(glGetQueryObjectuiv) ENTRY(glGetShaderInfoLog) ENTRY(glGetShaderiv) ENTRY(glGetString) ENTRY(glGetTexImage)
	ENTRY(glGetUniformLocation) ENTRY(glIsEnabled) ENTRY(glLineWidth) ENTRY(glLinkProgram) ENTRY(glMapBufferRange)
	ENTRY(glPixelStorei) ENTRY(glPolygonMode) ENTRY(glPolygonOffset) ENTRY(glReadPixels)
	ENTRY(glRenderbufferStorageMultisample) ENTRY(glSamplerParameterf) ENTRY(glSamplerParameterfv)
	ENTRY(glSamplerParameteri) ENTRY(glScissor) ENTRY(glShaderSource) ENTRY(glStencilFunc) ENTRY(glStencilMask)
	ENTRY(glStencilOp) ENTRY(glTexImage2D) ENTRY(glTexImage3D) ENTRY(glTexParameterf) ENTRY(glTexParameterfv)
	ENTRY(glTexParameteri) ENTRY(glTexParameteriv) ENTRY(glTexSubImage2D) ENTRY(glUniform1f) ENTRY(glUniform1i)
	ENTRY(glUniform1iv) ENTRY(glUniform2f) ENTRY(glUniform4fv) ENTRY(glUnmapBuffer) ENTRY(glUseProgram)
	ENTRY(glVertexAttrib4fv) ENTRY(glVertexAttribBinding) ENTRY(glVertexAttribFormat) ENTRY(glVertexAttribI4ui)
	ENTRY(glVertexAttribIFormat) ENTRY(glVertexAttribIPointer) ENTRY(glVertexAttribPointer) ENTRY(glViewport)
	/* (the optional ones the renderer manages without: glBufferStorage,
	glCopyImageSubData, glMemoryBarrier, glDebugMessageCallback) */
};

void *x360_gl_function(const char *name)
{
	size_t index;

	for (index = 0; index < sizeof(entries) / sizeof(entries[0]); index++)
	{
		if (!strcmp(entries[index].name, name))
			return entries[index].function;
	}
	return NULL;
}
