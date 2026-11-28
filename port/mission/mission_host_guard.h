#ifndef PORT_MISSION_HOST_GUARD_H
#define PORT_MISSION_HOST_GUARD_H

/* Host callbacks of the guard domain of the campaign API (#165): spawning, settings, movement, gestures and
 * animation, combat, perception. Guards are addressed by chr number (setup guards and spawned ones alike).
 * See mission_lua_ext.h for the layout; the script-side contract is port/mission/lib/mission.d.luau.
 *
 * Conventions: `chr` is the guard's chr number; `pad` an original pad number (a mission pad's string id is
 * resolved with pad_by_id first); `player` is 0-based, -1 = "the guard's player" (the nearest living one);
 * a callback that returns a count / flag returns 0 for a guard that is not there. */

#ifdef __cplusplus
extern "C" {
#endif

/* speeds */
#define MG_WALK   0
#define MG_RUN    1
#define MG_SPRINT 2

/* set_flag / get_flag */
enum {
    MG_FLAG_INVINCIBLE, MG_FLAG_NO_AUTOAIM, MG_FLAG_CAN_SHOOT_GUARDS, MG_FLAG_CIVILIAN, MG_FLAG_DORMANT,
    MG_FLAG_NO_BLOOD, MG_FLAG_FAST_SPRINT, MG_FLAG_NO_SHADOW, MG_FLAG_SUNGLASSES, MG_FLAG_CLONE_SENDER,
    MG_FLAG_COUNT
};

/* combat_move */
enum { MG_MOVE_ROLL_FIRE, MG_MOVE_WALK_FIRE, MG_MOVE_RUN_FIRE, MG_MOVE_SIDESTEP, MG_MOVE_SIDE_HOP, MG_MOVE_SIDE_RUN };

/* fire_at: what the target is */
enum { MG_TGT_PLAYER, MG_TGT_CHR, MG_TGT_PAD, MG_TGT_BODY };   /* BODY: bond_body(p), target = p (aims at that player's chr) */

/* witnessed */
#define MG_SAW_NONE  0
#define MG_SAW_HIT   1
#define MG_SAW_DEATH 2

/* set_stats: a field is set when its has_* bit is on (the d.luau GuardStats table). */
#define MG_ST_HEALTH       0x001
#define MG_ST_MAX_HEALTH   0x002
#define MG_ST_ARMOUR       0x004
#define MG_ST_SPEED        0x008
#define MG_ST_ACCURACY     0x010
#define MG_ST_REACTION     0x020
#define MG_ST_GRENADE      0x040
#define MG_ST_FLINCH       0x080
#define MG_ST_ADD_ARMOUR   0x100
#define MG_ST_HEARING      0x200
#define MG_ST_VISION       0x400
#define MG_ST_RECOVERY     0x800
typedef struct MissionGuardStats {
    unsigned has;                   /* MG_ST_* */
    double health, max_health, armour, speed, accuracy, reaction, grenade_chance;
    double add_armour, hearing, vision, recovery;   /* with their has bits */
    int flinch;                     /* with MG_ST_FLINCH: the chance a hit interrupts it, 0..100 (false 0, true 100) */
    int raw;                        /* no difficulty modifier */
} MissionGuardStats;

/* anim(): every field is optional; -1 / 0 = default */
typedef struct MissionGuardAnim {
    int from, to;                   /* frames; -1 = default (0 / the whole animation) */
    double speed;                   /* 1 = normal; 0 = default */
    int hold, reverse, loop, no_move;
    double blend;                   /* seconds; < 0 = default */
    int idle_after;                 /* -1: default (on unless hold), else 0 / 1 */
    int sound;                      /* an sfx id played with it; -1: none */
    int interruptible;              /* is_idle() is true while it plays */
} MissionGuardAnim;

/* spawn_guard() */
typedef struct MissionGuardSpawn {
    int chr;                        /* -1: any free number */
    int pad;
    int body, head;                 /* head -1: the game picks */
    int force, sunglasses;
} MissionGuardSpawn;

typedef struct MissionHostGuard {
    void *ud;

    /* presence */
    int  (*exists)(void *ud, int chr);
    int  (*is_dying)(void *ud, int chr);
    void (*chr_remove)(void *ud, int chr);
    void (*fade_out)(void *ud, int chr);
    int  (*teleport)(void *ud, int chr, int pad);
    void (*set_hidden)(void *ud, int chr, int on);
    int  (*spawn)(void *ud, const MissionGuardSpawn *s);          /* the new guard's chr number; -1: refused */
    int  (*set_ai)(void *ud, int chr, int list);                  /* works for any chr (spawned ones too); 0: no such chr */
    int  (*setup_ai)(void *ud, int chr);                          /* the AI list id the setup record gives it; -1: none */
    int  (*all_guards)(void *ud, int *props, int *chrs, int max); /* every spawned setup guard of this difficulty */
    int  (*pad_by_id)(void *ud, const char *id);                  /* a mission pad's number; -1: none */

    /* settings */
    void (*set_stats)(void *ud, int chr, const MissionGuardStats *st);
    void (*set_flag)(void *ud, int chr, int flag, int on);
    int  (*get_flag)(void *ud, int chr, int flag);
    int  (*give_weapon)(void *ud, int chr, int item, int model, int left_hand, int concealed);   /* model -1: the item's own */
    int  (*give_hat)(void *ud, int chr, int model);
    void (*drop_items)(void *ud, int chr);
    void (*remove_held_items)(void *ud, int chr);
    int  (*preset_pad)(void *ud, int chr);                        /* -1: none */
    int  (*held_weapon)(void *ud, int chr);                       /* the item in the right hand; -1: none */
    int  (*drop_object)(void *ud, int chr, int prop);             /* drops that carried object (setup prop); 0: it does not carry it */
    int  (*give_object)(void *ud, int chr, int prop);             /* it carries that object (setup prop) */
    void (*set_flag_raw)(void *ud, int chr, unsigned bits, int on);   /* raw chrflags */

    /* movement */
    void (*patrol)(void *ud, int chr, int path);
    int  (*is_patrolling)(void *ud, int chr);
    int  (*is_moving)(void *ud, int chr);                         /* on its way: go_to, run_to, run_to_player, patrol */
    int  (*run_to)(void *ud, int chr, int pad, int speed);        /* MG_*; 0: no route */
    int  (*run_to_player)(void *ud, int chr, int speed, int player);
    void (*face_player)(void *ud, int chr, int player);
    void (*face_pad)(void *ud, int chr, int pad);
    void (*halt)(void *ud, int chr);
    int  (*flee_pad)(void *ud, int chr);                          /* -1: none */
    int  (*cover_pad)(void *ud, int chr);
    int  (*spot_near_player)(void *ud, int chr, int player);      /* a pad near the player the guard can reach; -1: none */

    /* gestures and animation */
    void (*look_around)(void *ud, int chr);
    void (*point_at_player)(void *ud, int chr);
    void (*raise_arms)(void *ud, int chr);
    void (*kneel)(void *ud, int chr);
    void (*idle_anim)(void *ud, int chr);
    int  (*anim)(void *ud, int chr, int anim_id, const char *name, const MissionGuardAnim *o);   /* anim_id -1: by `name` */
    int  (*anim_done)(void *ud, int chr);
    void (*stop_anim)(void *ud, int chr);

    /* combat */
    int  (*fire_at)(void *ud, int chr, int kind, int target, int kneel, int aim_only);   /* kind MG_TGT_*; target a player / chr / pad */
    int  (*combat_move)(void *ud, int chr, int move, int player);
    int  (*throw_grenade)(void *ud, int chr, int player);
    int  (*grenade_chance)(void *ud, int chr);                    /* 0..100 */
    int  (*drop_grenade)(void *ud, int chr);                      /* a live grenade at its feet */
    void (*hit)(void *ud, int chr, int body_part, int item);
    int  (*is_busy)(void *ud, int chr);
    int  (*is_idle)(void *ud, int chr);
    int  (*clone)(void *ud, int chr);                             /* the clone's chr number; -1: cannot */
    int  (*press_alarm)(void *ud, int chr, int pad);

    /* perception */
    int  (*spotted_player)(void *ud, int chr);                    /* the player the engine's last sight check saw; -1: none.
                                                                     Latched, cleared by the read; the first call starts the checks */
    int  (*line_to_player)(void *ud, int chr);
    int  (*on_screen)(void *ud, int chr);
    int  (*was_seen)(void *ud, int chr);
    int  (*is_aimed_at)(void *ud, int chr);
    int  (*alerted_within)(void *ud, int chr, double seconds);
    int  (*heard_shot_within)(void *ud, int chr, double seconds);
    int  (*near_misses)(void *ud, int chr);
    int  (*was_shot_at)(void *ud, int chr);                       /* a player's shot passed it since the last call (latched) */
    int  (*witnessed)(void *ud, int chr, int *other);             /* MG_SAW_*; other: the chr it saw */
    double (*angle_to_player)(void *ud, int chr, int player);     /* degrees */
    int  (*was_damaged)(void *ud, int chr);                       /* test and clear */
    int  (*civilians_killed)(void *ud);
} MissionHostGuard;

#ifdef __cplusplus
}
#endif

#endif
