/*
X360_GLSL.C

The desktop renderer's GLSL as HLSL for the console's compiler (D3DX,
shader model 3). The renderer's shaders are generated (nv2a_vsh.c,
nv2a_psh.c, d3d8_gl.c), so their GLSL is a regular subset: global uniform,
in and out declarations, then functions. That is translated as text:

- declarations: uniforms stay uniforms; ins and outs become static globals,
  copied in and out by a main() of HLSL's own around the GLSL one (renamed
  glsl_main), the vertex shader's inputs by their layout locations
  (TEXCOORD0 to 15, as the vertex declarations name them, x360_gl.c), the
  varyings by name (TEXCOORDn, one number for each name, the same in every
  shader), gl_Position as POSITION, the fragment color as COLOR0;
- types: vecN, ivecN, bvecN and matN become HLSL's, and a vector
  constructor a call to helpers that take GLSL's forms (vec4(x), vec4(v3,
  w));
- functions: GLSL's names for HLSL's (fract, mix, inversesqrt, texture);
- NORMPACKED3 attributes, which GLSL unpacks with integer arithmetic shader
  model 3 does not have, arrive unpacked (the vertex declaration's
  10:11:11 format) and unpack_normpacked3 passes them on.
*/

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void x360_log(const char *format, ...);
#include "x360_glsl.h"

/* ---------- growing text */

struct text
{
	char *buffer;
	size_t length, capacity;
};

static void text_append_length(struct text *text, const char *data, size_t length)
{
	if (text->length + length + 1 > text->capacity)
	{
		size_t capacity = text->capacity ? text->capacity * 2 : 4096;
		char *grown;

		while (capacity < text->length + length + 1)
			capacity *= 2;
		grown = realloc(text->buffer, capacity);
		if (!grown)
			return;
		text->buffer = grown;
		text->capacity = capacity;
	}
	memcpy(text->buffer + text->length, data, length);
	text->length += length;
	text->buffer[text->length] = '\0';
}

static void text_append(struct text *text, const char *format, ...)
{
	char line[1024];
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	if (length < 0)
		return;
	if ((size_t)length < sizeof(line))
	{
		text_append_length(text, line, (size_t)length);
		return;
	}
	{
		char *long_line = malloc((size_t)length + 1);

		if (!long_line)
			return;
		va_start(arguments, format);
		vsnprintf(long_line, (size_t)length + 1, format, arguments);
		va_end(arguments);
		text_append_length(text, long_line, (size_t)length);
		free(long_line);
	}
}

/* ---------- varyings: one TEXCOORD number per name, for every shader */

#define MAXIMUM_VARYINGS 12

static char varying_names[MAXIMUM_VARYINGS][32];
static int varying_count;

static int varying_index(const char *name)
{
	int index;

	for (index = 0; index < varying_count; index++)
	{
		if (!strcmp(varying_names[index], name))
			return index;
	}
	if (varying_count == MAXIMUM_VARYINGS)
	{
		x360_log("glsl: more than %d varyings: %s shares the last", MAXIMUM_VARYINGS, name);
		return MAXIMUM_VARYINGS - 1;
	}
	strncpy(varying_names[varying_count], name, sizeof(varying_names[0]) - 1);
	return varying_count++;
}

/* ---------- declarations */

#define MAXIMUM_DECLARATIONS 64

struct declaration
{
	char storage[8];   /* "in", "out" */
	char type[16];     /* HLSL's */
	char name[48];
	int location;      /* layout(location = n), else -1 */
	int packed;        /* a uint NORMPACKED3 attribute */
};

/* GLSL type name to HLSL's, the same string if it has no other */
static const char *hlsl_type(const char *type)
{
	static const struct { const char *glsl, *hlsl; } types[] =
	{
		{ "vec2", "float2" }, { "vec3", "float3" }, { "vec4", "float4" },
		{ "ivec2", "int2" }, { "ivec3", "int3" }, { "ivec4", "int4" },
		{ "uvec2", "int2" }, { "uvec3", "int3" }, { "uvec4", "int4" },
		{ "bvec2", "bool2" }, { "bvec3", "bool3" }, { "bvec4", "bool4" },
		{ "mat2", "float2x2" }, { "mat3", "float3x3" }, { "mat4", "float4x4" },
		{ "uint", "int" },
		{ "samplerCube", "samplerCUBE" },
	};
	size_t index;

	for (index = 0; index < sizeof(types) / sizeof(types[0]); index++)
	{
		if (!strcmp(types[index].glsl, type))
			return types[index].hlsl;
	}
	return type;
}

/* the helper a constructor of a GLSL type calls, or NULL */
static const char *constructor(const char *name)
{
	static const struct { const char *glsl, *helper; } helpers[] =
	{
		{ "vec2", "glsl_vec2" }, { "vec3", "glsl_vec3" }, { "vec4", "glsl_vec4" },
		{ "ivec2", "glsl_vec2" }, { "ivec3", "glsl_vec3" }, { "ivec4", "glsl_vec4" },
	};
	size_t index;

	for (index = 0; index < sizeof(helpers) / sizeof(helpers[0]); index++)
	{
		if (!strcmp(helpers[index].glsl, name))
			return helpers[index].helper;
	}
	return NULL;
}

/* GLSL function names that are HLSL's under other names */
static const char *function_name(const char *name)
{
	static const struct { const char *glsl, *hlsl; } names[] =
	{
		{ "fract", "frac" }, { "mix", "lerp" }, { "inversesqrt", "rsqrt" }, { "mod", "glsl_mod" },
		{ "texture", "glsl_texture" }, { "textureLod", "glsl_texture_lod" }, { "dFdx", "ddx" },
		{ "dFdy", "ddy" }, { "atan", "glsl_atan" }, { "lessThan", "glsl_less_than" },
		{ "lessThanEqual", "glsl_less_than_equal" }, { "greaterThan", "glsl_greater_than" },
		{ "greaterThanEqual", "glsl_greater_than_equal" },
	};
	size_t index;

	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
	{
		if (!strcmp(names[index].glsl, name))
			return names[index].hlsl;
	}
	return NULL;
}

/* identifiers GLSL allows that HLSL reserves */
static int hlsl_reserved(const char *name)
{
	static const char *const reserved[] =
	{
		"sample", "sampler", "texture", "vector", "matrix", "line", "point", "triangle", "register", "pass",
		"technique", "half", "string", "compile", "shared", "snorm", "unorm", "linear", "centroid",
		"nointerpolation", "packoffset", "dword", "pixelfragment", "vertexfragment", "stateblock",
		"tbuffer", "cbuffer", "row_major", "column_major", "lineadj", "triangleadj", "precise", "groupshared",
	};
	size_t index;

	for (index = 0; index < sizeof(reserved) / sizeof(reserved[0]); index++)
	{
		if (!strcmp(reserved[index], name))
			return 1;
	}
	return 0;
}

/* ---------- the body: identifiers renamed, constructors made calls */

static const char *skip_space(const char *cursor)
{
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
		cursor++;
	return cursor;
}

static void translate_body(struct text *out, const char *body, int vertex)
{
	const char *cursor = body;

	while (*cursor)
	{
		if (cursor[0] == '/' && cursor[1] == '*')
		{
			const char *end = strstr(cursor + 2, "*/");

			end = end ? end + 2 : cursor + strlen(cursor);
			text_append_length(out, cursor, (size_t)(end - cursor));
			cursor = end;
			continue;
		}
		if (cursor[0] == '/' && cursor[1] == '/')
		{
			const char *end = strchr(cursor, '\n');

			end = end ? end : cursor + strlen(cursor);
			text_append_length(out, cursor, (size_t)(end - cursor));
			cursor = end;
			continue;
		}
		if (isalpha((unsigned char)*cursor) || *cursor == '_')
		{
			char name[64];
			size_t length = 0;
			const char *after;
			const char *replacement;

			while ((isalnum((unsigned char)cursor[length]) || cursor[length] == '_') && length < sizeof(name) - 1)
			{
				name[length] = cursor[length];
				length++;
			}
			name[length] = '\0';
			after = skip_space(cursor + length);
			cursor += length;
			if (*after == '(' && (replacement = constructor(name)) != NULL)
				text_append(out, "%s", replacement);
			else if (*after == '(' && (replacement = function_name(name)) != NULL)
				text_append(out, "%s", replacement);
			else if (*after == '(' && !strcmp(name, "main"))
				text_append(out, "glsl_main");
			else if (!strcmp(name, "gl_Position"))
				text_append(out, "glsl_position");
			else if (!strcmp(name, "gl_PointSize"))
				text_append(out, "glsl_point_size");
			else if (!strcmp(name, "gl_FragCoord"))
				text_append(out, "glsl_frag_coord");
			else if (!strcmp(name, "gl_FrontFacing"))
				text_append(out, "glsl_front_facing");
			else if (!strcmp(name, "gl_FragDepth"))
				text_append(out, "glsl_frag_depth");
			else if (!strcmp(name, "discard"))
				/* (the console's compiler has no discard statement) */
				text_append(out, "clip(-1.0)");
			else if (hlsl_reserved(name))
				text_append(out, "glsl_%s", name);
			else
				text_append(out, "%s", hlsl_type(name));
			continue;
		}
		text_append_length(out, cursor, 1);
		cursor++;
	}
	(void)vertex;
}

/* ---------- declarations: read from the top of the source */

/* parses a global declaration line, "[layout(location = n)] storage type
name[...];"; nonzero if it is one */
static int parse_declaration(const char *line, struct declaration *declaration)
{
	char storage[16], type[32], name[64];
	const char *cursor = line;
	int location = -1;

	memset(declaration, 0, sizeof(*declaration));
	if (!strncmp(cursor, "layout(location = ", 18))
	{
		location = atoi(cursor + 18);
		cursor = strchr(cursor, ')');
		if (!cursor)
			return 0;
		cursor = skip_space(cursor + 1);
	}
	if (sscanf(cursor, "%15s %31s %63[^;]", storage, type, name) != 3)
		return 0;
	if (strcmp(storage, "in") && strcmp(storage, "out") && strcmp(storage, "uniform"))
		return 0;
	strncpy(declaration->storage, storage, sizeof(declaration->storage) - 1);
	declaration->packed = !strcmp(type, "uint");
	strncpy(declaration->type, declaration->packed ? "float4" : hlsl_type(type), sizeof(declaration->type) - 1);
	strncpy(declaration->name, name, sizeof(declaration->name) - 1);
	declaration->location = location;
	return 1;
}

static const char prelude[] =
	"float2 glsl_vec2(float x) { return float2(x, x); }\n"
	"float2 glsl_vec2(float2 v) { return v; }\n"
	"float2 glsl_vec2(float3 v) { return v.xy; }\n"
	"float2 glsl_vec2(float4 v) { return v.xy; }\n"
	"float2 glsl_vec2(float x, float y) { return float2(x, y); }\n"
	"float3 glsl_vec3(float x) { return float3(x, x, x); }\n"
	"float3 glsl_vec3(float3 v) { return v; }\n"
	"float3 glsl_vec3(float4 v) { return v.xyz; }\n"
	"float3 glsl_vec3(float x, float y, float z) { return float3(x, y, z); }\n"
	"float3 glsl_vec3(float2 v, float z) { return float3(v, z); }\n"
	"float3 glsl_vec3(float x, float2 v) { return float3(x, v); }\n"
	"float4 glsl_vec4(float x) { return float4(x, x, x, x); }\n"
	"float4 glsl_vec4(float4 v) { return v; }\n"
	"float4 glsl_vec4(float x, float y, float z, float w) { return float4(x, y, z, w); }\n"
	"float4 glsl_vec4(float3 v, float w) { return float4(v, w); }\n"
	"float4 glsl_vec4(float x, float3 v) { return float4(x, v); }\n"
	"float4 glsl_vec4(float2 a, float2 b) { return float4(a, b); }\n"
	"float4 glsl_vec4(float2 v, float z, float w) { return float4(v, z, w); }\n"
	"float4 glsl_vec4(float x, float y, float2 v) { return float4(x, y, v); }\n"
	"float4 glsl_vec4(float x, float2 v, float w) { return float4(x, v, w); }\n"
	"float glsl_mod(float x, float y) { return x - y * floor(x / y); }\n"
	"float2 glsl_mod(float2 x, float2 y) { return x - y * floor(x / y); }\n"
	"float3 glsl_mod(float3 x, float3 y) { return x - y * floor(x / y); }\n"
	"float4 glsl_mod(float4 x, float4 y) { return x - y * floor(x / y); }\n"
	"float4 glsl_less_than(float4 a, float4 b) { return a < b ? 1.0 : 0.0; }\n"
	"float4 glsl_less_than_equal(float4 a, float4 b) { return a <= b ? 1.0 : 0.0; }\n"
	"float4 glsl_greater_than(float4 a, float4 b) { return a > b ? 1.0 : 0.0; }\n"
	"float4 glsl_greater_than_equal(float4 a, float4 b) { return a >= b ? 1.0 : 0.0; }\n"
	"float glsl_atan(float y, float x) { return atan2(y, x); }\n"
	"float glsl_atan(float x) { return atan(x); }\n"
	"float4 glsl_texture(sampler2D s, float2 uv) { return tex2D(s, uv); }\n"
	"float4 glsl_texture(sampler3D s, float3 uvw) { return tex3D(s, uvw); }\n"
	"float4 glsl_texture(samplerCUBE s, float3 uvw) { return texCUBE(s, uvw); }\n"
	"float4 glsl_texture_lod(sampler2D s, float2 uv, float lod) { return tex2Dlod(s, float4(uv, 0.0, lod)); }\n"
	"static float4 glsl_position = float4(0.0, 0.0, 0.0, 1.0);\n"
	"static float glsl_point_size = 1.0;\n"
	"static float4 glsl_frag_coord = float4(0.0, 0.0, 0.0, 1.0);\n"
	"static bool glsl_front_facing = true;\n"
	"static float glsl_frag_depth = 0.0;\n";

/* the GLSL unpack_normpacked3 function: its integer arithmetic replaced */
static const char *skip_function(const char *source, const char *name)
{
	const char *start = strstr(source, name);
	const char *brace;
	int depth = 0;

	if (!start)
		return NULL;
	brace = strchr(start, '{');
	if (!brace)
		return NULL;
	for (; *brace; brace++)
	{
		if (*brace == '{')
			depth++;
		else if (*brace == '}' && --depth == 0)
			return brace + 1;
	}
	return NULL;
}

char *x360_glsl_to_hlsl(const char *source, int vertex)
{
	struct text out = { 0 };
	struct text body = { 0 };
	struct declaration declarations[MAXIMUM_DECLARATIONS];
	int declaration_count = 0, index;
	const char *cursor = source;
	int uses_frag_coord = strstr(source, "gl_FragCoord") != NULL;
	int uses_front_facing = strstr(source, "gl_FrontFacing") != NULL;
	int uses_frag_depth = strstr(source, "gl_FragDepth") != NULL;

	text_append(&out, "%s", prelude);
	/* line by line: declarations to the list, the rest to the body */
	while (*cursor)
	{
		const char *end = strchr(cursor, '\n');
		size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
		char line[512];
		struct declaration declaration;
		const char *trimmed;

		if (length >= sizeof(line))
		{
			/* (a long line is code) */
			text_append_length(&body, cursor, length);
			text_append_length(&body, "\n", 1);
			cursor += length + (end ? 1 : 0);
			continue;
		}
		memcpy(line, cursor, length);
		line[length] = '\0';
		cursor += length + (end ? 1 : 0);
		trimmed = skip_space(line);
		if (!strncmp(trimmed, "#version", 8) || !strncmp(trimmed, "precision ", 10) ||
			!strncmp(trimmed, "invariant ", 10) || !strncmp(trimmed, "#extension", 10))
		{
			continue;
		}
		if (trimmed == line && parse_declaration(trimmed, &declaration))
		{
			if (!strcmp(declaration.storage, "uniform"))
			{
				text_append(&out, "uniform %s %s;\n", declaration.type, declaration.name);
			}
			else if (declaration_count < MAXIMUM_DECLARATIONS)
			{
				declarations[declaration_count++] = declaration;
				text_append(&out, "static %s %s;\n", declaration.type, declaration.name);
			}
			continue;
		}
		if (!strncmp(trimmed, "vec4 unpack_normpacked3(", 24))
		{
			/* (to the end of the function) */
			const char *rest = skip_function(source, "vec4 unpack_normpacked3(");

			text_append(&body, "float4 unpack_normpacked3(float4 p) { return float4(p.xyz, 1.0); }\n");
			if (rest)
				cursor = rest;
			continue;
		}
		text_append_length(&body, line, length);
		text_append_length(&body, "\n", 1);
	}
	if (body.buffer)
		translate_body(&out, body.buffer, vertex);
	free(body.buffer);

	/* HLSL's main around GLSL's */
	text_append(&out, "struct glsl_inputs\n{\n");
	for (index = 0; index < declaration_count; index++)
	{
		struct declaration *declaration = &declarations[index];

		if (strcmp(declaration->storage, "in"))
			continue;
		if (vertex)
			text_append(&out, "\t%s %s : TEXCOORD%d;\n", declaration->type, declaration->name,
				declaration->location < 0 ? index : declaration->location);
		else
			text_append(&out, "\t%s %s : TEXCOORD%d;\n", declaration->type, declaration->name,
				varying_index(declaration->name));
	}
	if (!vertex && uses_frag_coord)
		text_append(&out, "\tfloat2 glsl_vpos : VPOS;\n");
	if (!vertex && uses_front_facing)
		text_append(&out, "\tfloat glsl_vface : VFACE;\n");
	if (!vertex && !uses_frag_coord && !uses_front_facing && !declaration_count)
		text_append(&out, "\tfloat4 glsl_unused : TEXCOORD0;\n");
	text_append(&out, "};\nstruct glsl_outputs\n{\n");
	if (vertex)
		text_append(&out, "\tfloat4 glsl_position : POSITION;\n");
	for (index = 0; index < declaration_count; index++)
	{
		struct declaration *declaration = &declarations[index];

		if (strcmp(declaration->storage, "out"))
			continue;
		if (vertex)
			text_append(&out, "\t%s %s : TEXCOORD%d;\n", declaration->type, declaration->name,
				varying_index(declaration->name));
		else
			text_append(&out, "\t%s %s : COLOR%d;\n", declaration->type, declaration->name,
				declaration->location < 0 ? 0 : declaration->location);
	}
	if (!vertex && uses_frag_depth)
		text_append(&out, "\tfloat glsl_depth : DEPTH;\n");
	text_append(&out, "};\nglsl_outputs main(glsl_inputs inputs)\n{\n\tglsl_outputs outputs;\n");
	for (index = 0; index < declaration_count; index++)
	{
		if (!strcmp(declarations[index].storage, "in"))
			text_append(&out, "\t%s = inputs.%s;\n", declarations[index].name, declarations[index].name);
	}
	if (!vertex && uses_frag_coord)
		text_append(&out, "\tglsl_frag_coord = float4(inputs.glsl_vpos + 0.5, 0.0, 1.0);\n");
	if (!vertex && uses_front_facing)
		text_append(&out, "\tglsl_front_facing = inputs.glsl_vface > 0.0;\n");
	text_append(&out, "\tglsl_main();\n");
	if (vertex)
		text_append(&out, "\toutputs.glsl_position = glsl_position;\n");
	for (index = 0; index < declaration_count; index++)
	{
		if (!strcmp(declarations[index].storage, "out"))
			text_append(&out, "\toutputs.%s = %s;\n", declarations[index].name, declarations[index].name);
	}
	if (!vertex && uses_frag_depth)
		text_append(&out, "\toutputs.glsl_depth = glsl_frag_depth;\n");
	text_append(&out, "\treturn outputs;\n}\n");
	return out.buffer;
}
