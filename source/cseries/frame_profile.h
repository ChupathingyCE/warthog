/*
FRAME_PROFILE.H

port: a frame's stages named for the Xbox 360's profile log
(port/xbox360/src/x360_profile.c); nothing on the other ports.
*/

#ifndef __FRAME_PROFILE_H
#define __FRAME_PROFILE_H
#pragma once

#ifdef HALO_XBOX360
void x360_profile_stage(const char *name);
#define FRAME_STAGE(name) x360_profile_stage(name)
#else
#define FRAME_STAGE(name) ((void)0)
#endif

#endif
