#ifndef PORT_MISSION_FIELDS_H
#define PORT_MISSION_FIELDS_H

/* The setup-record field tables (issue #101): ONE description of every non-pointer
 * field of a guard record (PROPDEF_GUARD) and of each object record type, shared by
 * the mission.json parse / write, mission_apply (clone-and-patch) and the level
 * export (level.json), so all three speak the same names.
 *
 * Offsets are into the PC-layout setup RECORD (the file mission_apply edits, which
 * the game loads in place: little-endian, pointer slots widened to 8 bytes), not the
 * N64 layout. levelexport.c static-asserts every offset against offsetof() of the
 * game structs in src/bondtypes.h. Left out on purpose: pointers (model, prop,
 * ailist, path, ...), the object's pad / model id / type and the guard's chrnum /
 * pad / body / AI list (the op's own keys: "pad", "model", "ai", "type"), and runtime
 * state (door position, firing angles, monitor animation, ...).
 *
 * JSON: a field is a key of its op ("flags": 4096). An object type's own fields sit in
 * a sub-object named for the type ("door": { "max_speed": 0.1 }), the header fields
 * every object record has (extrascale, state, flags, flags2, maxdamage, damage) are
 * flat. Every field is optional; absent = the template record's value. Numbers are
 * the field as the file holds it (u8.8 extrascale, the flag words), except 16.16 fixed-point words, which
 * are written as their value. Words whose meaning is unknown to the format stay raw integers.
 *
 *   guard (9):   extrascale state preset chrpreset1 health reaction_time bitflags
 *                head (-1 = random head, >= 0 that head). Body 0xFFFF = random body is
 *                the op's "model".
 *   object:      the six header fields above, plus per type: door key cctv magazine
 *                collectable monitor multi_monitor autogun ammo armour vehicle
 *                aircraft tank tinted_glass. Prop, alarm, rack, hat, gas, glass and
 *                safe records are the header fields only.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MF_FIXED: a 32-bit 16.16 fixed-point word in the file (the setup data stores most game floats this way: door speeds,
 * armour, health); the JSON and the API show the value (raw / 65536). MF_FLOAT: an IEEE float in the file (none yet). */
typedef enum MissionFieldKind { MF_UINT, MF_SINT, MF_FLOAT, MF_FIXED } MissionFieldKind;

typedef struct MissionField {
    const char *name;
    uint16_t off;                   /* byte offset in the PC-layout record */
    uint8_t size;                   /* 1, 2 or 4 (4 for MF_FLOAT / MF_FIXED) */
    uint8_t kind;                   /* MissionFieldKind */
    uint8_t grouped;                /* 1: lives in the type's own sub-object, 0: flat on the op */
} MissionField;

#define MISSION_MAX_FIELDS 48
#define MISSION_REC_GUARD  9

/* Which fields of an op were given (bit i = field i of missionRecFields(type)). */
typedef struct MissionFieldVals {
    uint64_t set;
    double v[MISSION_MAX_FIELDS];
} MissionFieldVals;

/* The fields of a record type, `type` = the PROPDEF number (9 = guard); 0 and NULL for a
 * type with no table (not a guard / object record). Header fields come first. */
int missionRecFields(int type, const MissionField **out);
/* The JSON sub-object name of the type's own fields, NULL when it has none. */
const char *missionRecGroup(int type);

/* Reads / writes one field of a record (little-endian bytes; floats widened to double). */
double missionFieldGet(const MissionField *f, const uint8_t *rec);
void   missionFieldSet(const MissionField *f, uint8_t *rec, double v);

/* The shortest decimal text that reads back as the same float. */
void   missionFloatText(char *buf, size_t n, float v);

/* Reads the fields of a record type out of a cJSON object (an op, or a level.json prop): what is
 * present is set, the rest stays absent. `obj` is a const cJSON *. */
void   missionFieldsFromJson(const void *obj, int rectype, MissionFieldVals *out);

/* The ,"name":value fragments for every field of a record, as a leading-comma JSON
 * member list ready to sit inside an op / prop object (grouped fields in their
 * sub-object). Returns the length written (0 for an unknown type); truncates at n. */
size_t missionRecJson(char *buf, size_t n, int type, const uint8_t *rec);

#ifdef __cplusplus
}
#endif

#endif
