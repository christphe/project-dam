#ifndef PORT_MISSION_HOST_WORLD_H
#define PORT_MISSION_HOST_WORLD_H

/* Host callbacks of the world domain of the campaign API (#165): objects by tag, doors, screens, vehicles and
 * aircraft, players, rooms, HUD text, the countdown, sound, music, fog, the alarm, mission end, objectives and the
 * world events. See mission_lua_ext.h for the layout; the script-side contract is port/mission/lib/mission.d.luau.
 *
 * A prop is a setup prop number (an object by tag(n) is resolved to one by the runtime). A NULL callback means "not
 * available": the binding returns the neutral value and logs "<name>: not implemented yet" once. Callbacks that act
 * on a prop return 0 when the prop is not what the verb needs (a guard for an object verb, a missing prop). */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* in_room() kinds: who is asked about */
enum { MHW_ROOM_CHR, MHW_ROOM_PROP, MHW_ROOM_PLAYER, MHW_ROOM_PAD };
/* door_phase() */
enum { MHW_DOOR_NONE = -1, MHW_DOOR_CLOSED, MHW_DOOR_OPENING, MHW_DOOR_OPEN, MHW_DOOR_CLOSING };
/* item_landed() results */
enum { MHW_LANDED_NONE, MHW_LANDED_STUCK, MHW_LANDED_LOOSE };
/* obj_set_damage() kinds */
enum { MHW_DMG_ALL, MHW_DMG_EXPLOSIONS, MHW_DMG_NONE };
/* sound_play() `from` kinds */
enum { MHW_FROM_NONE, MHW_FROM_PROP, MHW_FROM_PAD };

typedef struct MissionHostWorld {
    void *ud;

    /* rooms: is chr / setup prop / player / pad `index` in the room of the original pad number `pad`? */
    int  (*in_room)(void *ud, int kind, int index, int pad);
    int  (*player_in_room)(void *ud, int pad);                  /* the first player (0-based) in that room, -1: none */
    int  (*item_landed)(void *ud, int item, int *tag);          /* MHW_LANDED_*; for STUCK, *tag is the object tag it sticks to (-1: none) */
    int  (*same_room)(void *ud, int kind_a, int index_a, int kind_b, int index_b);   /* MHW_ROOM_* kinds: both in one room? */
    int  (*item_in_room)(void *ud, int item, int pad);          /* a loose thrown / planted weapon prop of that item in the room of that pad */

    /* objects */
    int  (*obj_destroyed)(void *ud, int prop);                  /* 1 destroyed, or not there; 0 healthy or not an object */
    int  (*obj_destroy)(void *ud, int prop);                    /* 0: not an object / not spawned */
    int  (*obj_explode)(void *ud, int prop);
    int  (*obj_set_invincible)(void *ud, int prop, int on);     /* 0: not an object (the caller then tries the guard domain) */
    int  (*obj_set_collectable)(void *ud, int prop, int on);
    int  (*obj_move_to)(void *ud, int prop, int pad);
    int  (*obj_activate)(void *ud, int prop);
    int  (*obj_show_screen)(void *ud, int prop, int bank, int screen);
    int  (*obj_held_by)(void *ud, int prop, int player);        /* player -1: any player */
    int  (*obj_set_rotor)(void *ud, int prop, double speed, double ramp_seconds);
    int  (*obj_drive)(void *ud, int prop, int path, double speed, double ramp_seconds);
    int  (*obj_set_active)(void *ud, int prop, int on);         /* 0: not a turret / drone gun / camera */
    int  (*obj_set_damage)(void *ud, int prop, int kind);       /* MHW_DMG_*; 0: not an object */
    int  (*obj_set_hidden)(void *ud, int prop, int on);         /* 0: not an object (the caller then tries the guard domain) */
    /* an aircraft's own model animation (AI_PlayAnimation on an aircraft); from / to frames (-1: default), speed 1 = normal,
     * blend in seconds (-1: default); 0: not an aircraft or an unknown animation */
    int  (*obj_anim)(void *ud, int prop, int id, int from, int to, double speed, double blend);
    int  (*give_object)(void *ud, int prop, int player);        /* into the player's inventory (-1: every player); 0: nothing to give */

    /* doors */
    int  (*door_phase)(void *ud, int prop);                     /* MHW_DOOR_*, MHW_DOOR_NONE: not a door */
    int  (*door_open_now)(void *ud, int prop);
    int  (*door_set_passable)(void *ud, int prop, int on);
    int  (*door_set_usable_by_guards)(void *ud, int prop, int on);

    /* players */
    int  (*player_set_invincible)(void *ud, int player, int on);
    int  (*player_armour)(void *ud, int player, double *out);   /* 0..1; 0: no such player */
    int  (*player_held_item)(void *ud, int player);             /* the item in the right hand, 1 = unarmed */
    int  (*player_holds)(void *ud, int player, int item);       /* in either hand */
    int  (*player_force_weapon)(void *ud, int player, int item);
    int  (*player_push)(void *ud, int player, double dx, double dz);
    int  (*player_health)(void *ud, int player, double *out);   /* 0..1; 0: no such player */
    int  (*player_kill)(void *ud, int player);
    int  (*explosions_around_players)(void *ud, int on);        /* endless explosions around every living player */
    int  (*killed_in_action)(void *ud);                         /* the game's KIA mark; the caller then fails the level */
    /* The direction a chr (kind MHW_ROOM_CHR, index = chr number) or a player (MHW_ROOM_PLAYER, 0-based) faces, as the
     * camera API's yaw in degrees (-180..180: 0 looks along +z, positive turns left, towards +x; the same yaw as
     * camera.set's CameraPoint.yaw). 0: not placed / not such a target. The game's own angle is converted by the host. */
    int  (*facing)(void *ud, int kind, int index, double *out_deg);
    int  (*player_eye)(void *ud, int player, double out[3]);    /* the player's eye point in world units; 0: not placed */

    /* HUD, countdown, sound, music, fog */
    int  (*hud_top)(void *ud, const char *text);
    int  (*hud_hide)(void *ud, int on);
    int  (*countdown_set)(void *ud, double seconds);
    int  (*countdown_run)(void *ud, int on);                    /* start / stop */
    int  (*countdown_show)(void *ud, int on);
    double (*countdown_left)(void *ud);
    int  (*sound_play)(void *ud, int id, int channel, int from_kind, int from_index);   /* channel -1: any free; returns the channel, -1: none */
    int  (*sound_stop)(void *ud, int channel);
    int  (*sound_volume)(void *ud, int channel, double volume, double fade_seconds);
    int  (*music_play)(void *ud, int slot, int stop_after, int total, double fade);
    int  (*music_stop)(void *ud, int slot);
    int  (*fog_next)(void *ud, int slow);

    /* alarm and the end of the level */
    int  (*alarm_is_on)(void *ud);
    int  (*alarm_stop)(void *ud);
    int  (*exiting)(void *ud);

    /* objectives.fail_all: the mission's own objectives, by index (the runtime cannot list their ids): 0 past the end */
    int  (*objective_id)(void *ud, int index, char *out, size_t outlen, int *difficulty);
} MissionHostWorld;

#ifdef __cplusplus
}
#endif

#endif
