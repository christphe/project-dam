/* The asset packs' sound effects (port/src/sounds.c, docs/specs/audio.md): sfx.json parsing and the WAV reader. */
#include "ge_test.h"
#include "sounds.h"

#include <stdint.h>
#include <string.h>

static const char *kJson =
    "[\n"
    "  {\"id\": 1, \"wav\": \"sfx_0001.wav\", \"rate_hz\": 22050, \"pitch_cents\": 0, \"volume_group\": 3, \"reverb\": 5, \"flags\": 1, "
    "\"retrigger\": true, \"pitch_slide_cents\": null, \"next\": 300, \"next_delay_ms\": 100, \"sample_volume\": 127, \"pan\": 64, "
    "\"envelope\": {\"attack_us\": 1000, \"attack_volume\": 127, \"decay_us\": -1, \"decay_volume\": 100, \"release_us\": 2000}, "
    "\"loop\": {\"start\": 10, \"end\": 90}},\n"
    "  {\"id\": 7, \"wav\": \"sfx_0007.wav\", \"rate_hz\": 20000, \"pitch_cents\": -250, \"volume_group\": 0, \"reverb\": 0, \"flags\": 2, "
    "\"retrigger\": false, \"pitch_slide_cents\": -12, \"next\": 0, \"next_delay_ms\": 0, \"sample_volume\": 90, \"pan\": 10, "
    "\"envelope\": {\"attack_us\": 0, \"attack_volume\": 127, \"decay_us\": 500000, \"decay_volume\": 127, \"release_us\": 0}, "
    "\"loop\": null, \"future\": [1, {\"a\": \"}\"}]}\n"
    "]\n";

static void le32(unsigned char *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

int main(void)
{
    static SfxEffect eff[16];
    unsigned char wav[44 + 8];
    const unsigned char *pcm = NULL;
    uint32_t ns = 0;

    GE_TEST_BEGIN("sounds");

    GE_CHECK_EQ_INT(sfxParseJson(kJson, strlen(kJson), eff, 16), 2);
    GE_CHECK(eff[1].present);
    GE_CHECK_EQ_INT(eff[1].volume_group, 3);
    GE_CHECK_EQ_INT(eff[1].reverb, 5);
    GE_CHECK_EQ_INT(eff[1].flags, 1);
    GE_CHECK_EQ_INT(eff[1].retrigger, 1);
    GE_CHECK_EQ_INT(eff[1].has_slide, 0);
    GE_CHECK_EQ_INT(eff[1].next, 300);
    GE_CHECK_EQ_INT(eff[1].next_delay_ms, 100);
    GE_CHECK_EQ_INT(eff[1].sample_volume, 127);
    GE_CHECK_EQ_INT(eff[1].pan, 64);
    GE_CHECK_EQ_INT(eff[1].attack_us, 1000);
    GE_CHECK_EQ_INT(eff[1].decay_us, -1);
    GE_CHECK_EQ_INT(eff[1].decay_vol, 100);
    GE_CHECK_EQ_INT(eff[1].release_us, 2000);
    GE_CHECK_EQ_INT(eff[1].has_loop, 1);
    GE_CHECK_EQ_INT(eff[1].loop_start, 10);
    GE_CHECK_EQ_INT(eff[1].loop_end, 90);
    GE_CHECK(eff[7].present);
    GE_CHECK_EQ_INT(eff[7].pitch_cents, -250);
    GE_CHECK_EQ_INT(eff[7].has_slide, 1);
    GE_CHECK_EQ_INT(eff[7].slide_cents, -12);
    GE_CHECK_EQ_INT(eff[7].has_loop, 0);
    GE_CHECK(!eff[2].present);
    GE_CHECK_EQ_INT(sfxParseJson("{}", 2, eff, 16), -1);
    GE_CHECK_EQ_INT(sfxParseJson("[{\"id\": 3", 9, eff, 16), -1);
    GE_CHECK_EQ_INT(sfxParseJson("[]", 2, eff, 16), 0);

    memcpy(wav, "RIFF", 4); le32(wav + 4, 36 + 8); memcpy(wav + 8, "WAVEfmt ", 8); le32(wav + 16, 16);
    wav[20] = 1; wav[21] = 0; wav[22] = 1; wav[23] = 0; le32(wav + 24, 22050); le32(wav + 28, 44100);
    wav[32] = 2; wav[33] = 0; wav[34] = 16; wav[35] = 0; memcpy(wav + 36, "data", 4); le32(wav + 40, 8);
    wav[44] = 1; wav[45] = 0; wav[46] = 0xFF; wav[47] = 0xFF;
    GE_CHECK(sfxWavSamples(wav, sizeof wav, &pcm, &ns));
    GE_CHECK_EQ_INT(ns, 4);
    GE_CHECK(pcm == wav + 44);
    GE_CHECK(!sfxWavSamples(wav, 40, &pcm, &ns));            /* cut short */
    wav[22] = 2;
    GE_CHECK(!sfxWavSamples(wav, sizeof wav, &pcm, &ns));    /* stereo */

    /* the stack generation check: a change is seen once, then the new generation is the built one */
    {
        unsigned built = 5;
        GE_CHECK_EQ_INT(sfxGenChanged(&built, 5), 0);
        GE_CHECK_EQ_INT(sfxGenChanged(&built, 6), 1);
        GE_CHECK_EQ_INT(built, 6);
        GE_CHECK_EQ_INT(sfxGenChanged(&built, 6), 0);
        GE_CHECK_EQ_INT(sfxGenChanged(&built, 0), 1);   /* wrap-around counts as a change too */
    }

    /* no sfx.json in any pack: the pack sounds are in use and every effect is silent (no fallback to the ROM bank) */
    sfxInit();
    GE_CHECK_EQ_INT(sfxActive(), 1);
    GE_CHECK(sfxGet(1) == NULL);
    GE_CHECK_EQ_INT(sfxCount(), 0);
    {
        unsigned char buf[8];
        memset(buf, 0xAA, sizeof buf);
        GE_CHECK_EQ_INT(sfxFetch(SFX_ADDR_BASE, buf, sizeof buf), 1);
        GE_CHECK_EQ_INT(buf[0], 0);
    }

    return GE_TEST_END();
}
