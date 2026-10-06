/*
XBOX_COOP.C

The original Xbox build has no network co-op (decided October 6,
2026): OpenCE's (port/linux/game/network_coop.c, coop_enemies.c,
coop_scripts.c, coop_spectate.c) is not built for the console, and these
answer for it as those do on a machine that is in no co-op game. The
game's own split-screen and campaign play as the Xbox's always did.

The console also never joins a co-op game: network_client_message_handler.c
leaves a host's game whose settings turn out to be co-op, and the System
Link list and ONLINE GAMES leave co-op games out (xbox_coop_game).
*/

#include "cseries.h"
#include "camera/dead_camera.h"
#include "game/game.h"
#include "game/game_engine.h"

#include "network_coop.h"
#include "coop_enemies.h"
#include "coop_scripts.h"
#include "coop_spectate.h"

/* ---------- a co-op game, as the console tells one */

/* a host's game is co-op if it has no game engine (a co-op round's
variant is empty: network_server_manager.c's
network_game_server_cooperative_round) */
boolean xbox_coop_game(
	long engine_type)
{
	return engine_type == game_engine_none;
}

/* ---------- network_coop.h */

boolean network_coop_active(void)
{
	return FALSE;
}

boolean network_coop_player_collisions(void)
{
	return TRUE;
}

boolean network_coop_devices_remote(void)
{
	return FALSE;
}

void network_coop_note_device_snap(short group_index)
{
	(void)group_index;
}

boolean network_coop_set_players_vitality(long unit_index, boolean maximum, real body, real shield)
{
	(void)unit_index;
	(void)maximum;
	(void)body;
	(void)shield;
	return FALSE;
}

boolean network_coop_player_has_structure_bsp(long player_index)
{
	(void)player_index;
	return TRUE;
}

void network_coop_note_player_structure_bsp(short player_index, short structure_bsp_index)
{
	(void)player_index;
	(void)structure_bsp_index;
}

void network_coop_vehicle_dropped(long vehicle_index, long carrier_index)
{
	(void)vehicle_index;
	(void)carrier_index;
}

void network_coop_note_title(short title_index, real delay)
{
	(void)title_index;
	(void)delay;
}

void network_coop_note_hud(short kind, short value)
{
	(void)kind;
	(void)value;
}

void network_coop_note_player_effect(short kind, real a, real b, real c)
{
	(void)kind;
	(void)a;
	(void)b;
	(void)c;
}

void network_coop_note_nav_point(short kind, short nav_index, long target, long marker, real vertical_offset)
{
	(void)kind;
	(void)nav_index;
	(void)target;
	(void)marker;
	(void)vertical_offset;
}

void network_coop_note_unit_animation(long unit_index, long animation_graph_index, short animation_index,
	boolean interpolate)
{
	(void)unit_index;
	(void)animation_graph_index;
	(void)animation_index;
	(void)interpolate;
}

void network_coop_note_unit_open(long unit_index, boolean open)
{
	(void)unit_index;
	(void)open;
}

void network_coop_note_unit_animation_frame(long unit_index, short frame_index)
{
	(void)unit_index;
	(void)frame_index;
}

void network_coop_note_effect(long effect_definition_index, short cutscene_flag_index)
{
	(void)effect_definition_index;
	(void)cutscene_flag_index;
}

void network_coop_note_attach(long parent_index, char const *parent_marker_name, long child_index,
	char const *child_marker_name)
{
	(void)parent_index;
	(void)parent_marker_name;
	(void)child_index;
	(void)child_marker_name;
}

void network_coop_note_detach(long parent_index, long child_index)
{
	(void)parent_index;
	(void)child_index;
}

void network_coop_note_object_effect(long effect_definition_index, long object_index, char const *marker_name)
{
	(void)effect_definition_index;
	(void)object_index;
	(void)marker_name;
}

void network_coop_note_surface_broken(short breakable_surface_index, real_point3d const *epicenter)
{
	(void)breakable_surface_index;
	(void)epicenter;
}

void network_coop_note_scenery_animation(long object_index, long animation_graph_index, short animation_index,
	short frame_index)
{
	(void)object_index;
	(void)animation_graph_index;
	(void)animation_index;
	(void)frame_index;
}

boolean network_coop_skip_offered(void)
{
	return FALSE;
}

boolean network_coop_vote_skip(void)
{
	return FALSE;
}

void network_coop_reverted(long now)
{
	(void)now;
}

void network_coop_skip_done(void)
{
}

boolean network_coop_skip_vote_status(short *votes, short *voters, boolean *voted)
{
	(void)votes;
	(void)voters;
	(void)voted;
	return FALSE;
}

/* ---------- coop_spectate.h */

boolean coop_spectating(
	void)
{
	return FALSE;
}

long coop_spectate_unit(
	short local_player_index)
{
	(void)local_player_index;
	return NONE;
}

boolean coop_spectate_watching_rider(
	short local_player_index)
{
	(void)local_player_index;
	return FALSE;
}

boolean coop_spectate_nothing_to_watch(
	short local_player_index)
{
	(void)local_player_index;
	return FALSE;
}

void coop_spectate_camera(
	struct dead_camera *camera)
{
	(void)camera;
}

void coop_spectate_draw(
	short local_player_index)
{
	(void)local_player_index;
}

void coop_skip_vote_draw(
	short local_player_index)
{
	(void)local_player_index;
}

/* ---------- coop_scripts.h */

boolean coop_scripts_any_player_will_do(
	long object_list_index)
{
	(void)object_list_index;
	return FALSE;
}

void coop_scripts_teleport_followers(
	long unit_index)
{
	(void)unit_index;
}

void coop_scripts_suspend_followers(
	long unit_index,
	boolean suspended)
{
	(void)unit_index;
	(void)suspended;
}

void coop_scripts_exit_followers(
	long unit_index,
	long vehicle_index)
{
	(void)unit_index;
	(void)vehicle_index;
}

void coop_scripts_board_followers(
	long unit_index,
	long vehicle_index,
	char const *seat_name)
{
	(void)unit_index;
	(void)vehicle_index;
	(void)seat_name;
}

/* (as coop_scripts.c's off a co-op host: the game's own answer) */
boolean coop_scripts_safe_to_save(
	void)
{
	return game_safe_to_save();
}

void coop_scripts_player_add_equipment(
	long unit_index,
	short starting_profile_index,
	boolean reset_equipment)
{
	(void)unit_index;
	(void)starting_profile_index;
	(void)reset_equipment;
}

/* ---------- coop_enemies.h */

void coop_enemies_new_game(
	void)
{
}

void coop_enemies_reset(
	void)
{
}

short coop_enemies_extra_count(
	long encounter_index,
	short count)
{
	(void)encounter_index;
	(void)count;
	return 0;
}

boolean coop_enemies_spread_position(
	real_point3d const *origin,
	short number,
	long *taken,
	real_point3d *position)
{
	(void)origin;
	(void)number;
	(void)taken;
	(void)position;
	return FALSE;
}

boolean coop_enemies_fallback_position(
	real_point3d const *origin,
	short number,
	real_point3d *position)
{
	(void)origin;
	(void)number;
	(void)position;
	return FALSE;
}

void coop_enemies_rider_seated(
	long vehicle_index,
	long unit_index)
{
	(void)vehicle_index;
	(void)unit_index;
}

boolean coop_enemies_rider_unseated(
	long vehicle_index,
	long unit_index)
{
	(void)vehicle_index;
	(void)unit_index;
	return FALSE;
}

void coop_enemies_update(
	void)
{
}

/* ---------- the distributed netcode's co-op and AI sync hooks
(network_distributed.h): never sent by the console, and nothing done with
what a host sends (the console leaves a co-op game before any). AI sync
(port/linux/game/network_actors.c) is co-op's too: the Xbox's multiplayer
maps have no AI. An entry size of 0 only lets the netcode's bounds check
pass a message these then ignore. */

struct unit_control_data;

void network_actors_new_game(void)
{
}

void network_actors_host_tick(void)
{
}

void network_actors_handle_states(void const *entries, short count)
{
	(void)entries;
	(void)count;
}

word network_actors_entry_size(void)
{
	return 0;
}

void network_actors_drive(void)
{
}

word network_actors_damage_entry_size(void)
{
	return 0;
}

void network_actors_handle_damage(void const *entries, short count)
{
	(void)entries;
	(void)count;
}

void network_actors_note_control(long unit_index, struct unit_control_data const *control_data)
{
	(void)unit_index;
	(void)control_data;
}

void network_actors_note_impulse(long unit_index, short animation_impulse, real_vector2d const *alignment_vector)
{
	(void)unit_index;
	(void)animation_impulse;
	(void)alignment_vector;
}

void network_actors_note_melee(long unit_index, real_vector2d const *alignment_vector)
{
	(void)unit_index;
	(void)alignment_vector;
}

void network_actors_note_leap(long unit_index, real_vector2d const *alignment_vector)
{
	(void)unit_index;
	(void)alignment_vector;
}

void network_actors_note_user_animation(long unit_index, long animation_graph_index, short animation_index,
	boolean interpolate)
{
	(void)unit_index;
	(void)animation_graph_index;
	(void)animation_index;
	(void)interpolate;
}

void network_actors_note_speech(long unit_index, long sound_definition_index)
{
	(void)unit_index;
	(void)sound_definition_index;
}

void network_coop_new_game(void)
{
}

void network_coop_host_tick(void)
{
}

void network_coop_client_tick(void)
{
}

word network_coop_presentation_entry_size(void)
{
	return 0;
}

void network_coop_handle_presentation(void const *entries, long host_time)
{
	(void)entries;
	(void)host_time;
}

word network_coop_event_entry_size(void)
{
	return 0;
}

void network_coop_handle_events(void const *entries, short count)
{
	(void)entries;
	(void)count;
}

word network_coop_device_group_entry_size(void)
{
	return 0;
}

void network_coop_handle_device_groups(void const *entries, short count)
{
	(void)entries;
	(void)count;
}

word network_coop_object_names_entry_size(void)
{
	return 0;
}

void network_coop_handle_object_names(void const *entries)
{
	(void)entries;
}

word network_coop_object_transform_entry_size(void)
{
	return 0;
}

void network_coop_handle_object_transforms(void const *entries, short count)
{
	(void)entries;
	(void)count;
}

word network_coop_object_look_entry_size(void)
{
	return 0;
}

void network_coop_handle_object_looks(void const *entries, short count)
{
	(void)entries;
	(void)count;
}

word network_coop_screen_effect_entry_size(void)
{
	return 0;
}

void network_coop_handle_screen_effect(void const *entries, short count)
{
	(void)entries;
	(void)count;
}

word network_coop_device_state_entry_size(void)
{
	return 0;
}

void network_coop_handle_device_states(void const *entries, short count)
{
	(void)entries;
	(void)count;
}

word network_coop_skip_vote_entry_size(void)
{
	return 0;
}

void network_coop_handle_skip_vote(long machine_index, void const *entries)
{
	(void)machine_index;
	(void)entries;
}

void network_coop_note_sound(short kind, long definition_index, long object_index, real scale)
{
	(void)kind;
	(void)definition_index;
	(void)object_index;
	(void)scale;
}
