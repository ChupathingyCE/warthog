/*
X360_GLSL.H

The renderer's GLSL as HLSL (x360_glsl.c), for x360_gl.c.
*/

#ifndef __X360_GLSL_H
#define __X360_GLSL_H

/* HLSL for a vertex (vertex nonzero) or fragment shader's GLSL; malloc'd */
char *x360_glsl_to_hlsl(const char *source, int vertex);

#endif
