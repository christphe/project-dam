/* char_labels.h -- id -> file name and label of every character table entry, copied from the editor's
 * model_names.h (names only). Used by char_export.c for characters.json "label" and as a cross-check of the table. */
#ifndef PORT_EXPORT_CHAR_LABELS_H
#define PORT_EXPORT_CHAR_LABELS_H

static const struct { const char *file, *label; } k_char_labels[80] = {
    { "CcamguardZ", "Jungle_Commando" },   /* 0 */
    { "CgreyguardZ", "St_Petersburg_Guard" },   /* 1 */
    { "ColiveguardZ", "Russian_Soldier" },   /* 2 */
    { "CrusguardZ", "Russian_Infantry" },   /* 3 */
    { "CtrevguardZ", "Janus_Special_Forces" },   /* 4 */
    { "CdjbondZ", "Brosnan_Tuxedo" },   /* 5 */
    { "CborisZ", "Boris" },   /* 6 */
    { "CorumovZ", "Ourumov" },   /* 7 */
    { "CtrevelyanZ", "Trevelyan_Janus" },   /* 8 */
    { "CboilertrevZ", "Trevelyan_006" },   /* 9 */
    { "CvalentinZ", "Valentin_" },   /* 10 */
    { "CxeniaZ", "Xenia" },   /* 11 */
    { "CbaronsamediZ", "Baron_Samedi" },   /* 12 */
    { "CjawsZ", "Jaws" },   /* 13 */
    { "CmaydayZ", "Mayday" },   /* 14 */
    { "CoddjobZ", "Oddjob" },   /* 15 */
    { "CnatalyaZ", "Natalya_Skirt" },   /* 16 */
    { "CarmourguardZ", "Janus_Marine" },   /* 17 */
    { "CcommguardZ", "Russian_Commandant" },   /* 18 */
    { "CgreatguardZ", "Siberian_Guard_1_Mishkin" },   /* 19 */
    { "CnavyguardZ", "Naval_Officer" },   /* 20 */
    { "CsnowguardZ", "Siberian_Special_Forces" },   /* 21 */
    { "CboilerbondZ", "Special_Operations_Uniform" },   /* 22 */
    { "CsuitbondZ", "Formal_Wear" },   /* 23 */
    { "CtimberbondZ", "Jungle_Fatigues" },   /* 24 */
    { "CsnowbondZ", "Parka" },   /* 25 */
    { "CbluewomanZ", "Unused_Female" },   /* 26 */
    { "CfattechwomanZ", "Rosika" },   /* 27 */
    { "CtechwomanZ", "Scientist_2_Female" },   /* 28 */
    { "CjeanwomanZ", "Civilian_1_Female" },   /* 29 */
    { "CgreymanZ", "Unused_Male_1" },   /* 30 */
    { "CbluemanZ", "Unused_Male_2" },   /* 31 */
    { "CredmanZ", "Civilian_4" },   /* 32 */
    { "CcardimanZ", "Civilian_2" },   /* 33 */
    { "CcheckmanZ", "Civilian_3" },   /* 34 */
    { "CtechmanZ", "Scientist_1_Male" },   /* 35 */
    { "CpilotZ", "Helicopter_Pilot" },   /* 36 */
    { "Cgreatguard2Z", "Siberian_Guard_2" },   /* 37 */
    { "CbluecamguardZ", "Arctic_Commando" },   /* 38 */
    { "CmoonguardZ", "Moonraker_Elite_1_Male" },   /* 39 */
    { "CmoonfemaleZ", "Moonraker_Elite_2_Female" },   /* 40 */
    { "Csuit_lf_handZ", "Left_Suit_Hand_Floating_Arm" },   /* 41 */
    { "CheadkarlZ", "Male_Karl" },   /* 42 */
    { "CheadalanZ", "Male_Alan" },   /* 43 */
    { "CheadpeteZ", "Male_Pete" },   /* 44 */
    { "CheadmartinZ", "Male_Martin" },   /* 45 */
    { "CheadmarkZ", "Male_Mark" },   /* 46 */
    { "CheadduncanZ", "Male_Duncan" },   /* 47 */
    { "CheadshaunZ", "Male_Shaun" },   /* 48 */
    { "CheaddwayneZ", "Male_Dwayne" },   /* 49 */
    { "CheadbZ", "Male_B" },   /* 50 */
    { "CheaddaveZ", "Male_Dave_Dr_Doak" },   /* 51 */
    { "CheadgrantZ", "Male_Grant" },   /* 52 */
    { "CheaddesZ", "Male_Des" },   /* 53 */
    { "CheadchrisZ", "Male_Chris" },   /* 54 */
    { "CheadleeZ", "Male_Lee" },   /* 55 */
    { "CheadneilZ", "Male_Neil" },   /* 56 */
    { "CheadjimZ", "Male_Jim" },   /* 57 */
    { "CheadrobinZ", "Male_Robin" },   /* 58 */
    { "CheadstevehZ", "Male_Steve_H" },   /* 59 */
    { "CheadbalaclavaZ", "Male_Terrorist" },   /* 60 */
    { "CheadbikeZ", "Male_Biker" },   /* 61 */
    { "CheadgrahamZ", "Male_Graeme" },   /* 62 */
    { "CheadsteveeZ", "Male_Steve_Ellis" },   /* 63 */
    { "CheadjoelZ", "Male_Joel" },   /* 64 */
    { "CheadscottZ", "Male_Scott" },   /* 65 */
    { "CheadjoeZ", "Male_Joe_Altered" },   /* 66 */
    { "CheadkenZ", "Male_Ken" },   /* 67 */
    { "Cheadjoe2Z", "Male_Joe" },   /* 68 */
    { "CheadmishkinZ", "Male_Mishkin" },   /* 69 */
    { "CheadsallyZ", "Female_Sally" },   /* 70 */
    { "CheadmarionZ", "Female_Marion_Rosika" },   /* 71 */
    { "CheadmandyZ", "Female_Mandy" },   /* 72 */
    { "CheadvivienZ", "Female_Vivien" },   /* 73 */
    { "CheadbrosnanboilerZ", "Male_Pierce_Bond_1" },   /* 74 */
    { "CheadbrosnansuitZ", "Male_Pierce_Bond_2" },   /* 75 */
    { "CheadbrosnantimberZ", "Male_Pierce_Bond_3" },   /* 76 */
    { "CheadbrosnansnowZ", "Male_Pierce_Bond_Parka" },   /* 77 */
    { "CheadbrosnanZ", "Male_Pierce_Bond_Tuxedo" },   /* 78 */
    { "CspicebondZ", "Natalya_Jungle_Fatigues" },   /* 79 */
};

#endif
