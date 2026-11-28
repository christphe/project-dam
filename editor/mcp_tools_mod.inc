// MCP tools of the mod (campaign) panel and the environment. Included by mcp.cpp, inside its
// anonymous namespace (uses its helpers: ToolResult, fail, okJson, argStr, argInt, argHas).

cJSON *modJson(const Doc &d)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "dir", d.modDir.c_str());
    cJSON_AddStringToObject(o, "name", d.camp.name);
    cJSON_AddStringToObject(o, "author", d.camp.author);
    cJSON_AddStringToObject(o, "description", d.camp.description);
    cJSON *af = cJSON_AddArrayToObject(o, "affects");
    if (d.camp.affects & CAMPAIGN_AFFECTS_STORY) cJSON_AddItemToArray(af, cJSON_CreateString("story"));
    if (d.camp.affects & CAMPAIGN_AFFECTS_MULTIPLAYER) cJSON_AddItemToArray(af, cJSON_CreateString("multiplayer"));
    cJSON_AddStringToObject(o, "unlock", d.camp.unlock == CAMPAIGN_UNLOCK_OPEN ? "open" : "ordered");
    cJSON *ms = cJSON_AddArrayToObject(o, "missions");
    for (int i = 0; i < d.camp.nmissions; i++) cJSON_AddItemToArray(ms, cJSON_CreateString(d.camp.missions[i]));
    cJSON_AddBoolToObject(o, "dirty", d.modDirty);
    cJSON_AddStringToObject(o, "id", d.camp.id);
    cJSON_AddBoolToObject(o, "id_valid", d.modIdOk());
    if (!d.modNotice.empty()) cJSON_AddStringToObject(o, "warning", d.modNotice.c_str());
    return o;
}

ToolResult toolModOpen(Doc &d, const cJSON *a)
{
    std::string dir = argStr(a, "dir"), err;
    if (dir.empty()) return fail("\"dir\" is required: the mod folder (mods/<mod>); it is created when it does not exist");
    if (!d.openMod(dir, err)) return fail(err);
    if (argHas(a, "name")) snprintf(d.camp.name, sizeof d.camp.name, "%s", argStr(a, "name").c_str());
    if (argHas(a, "author")) snprintf(d.camp.author, sizeof d.camp.author, "%s", argStr(a, "author").c_str());
    if (argHas(a, "description")) snprintf(d.camp.description, sizeof d.camp.description, "%s", argStr(a, "description").c_str());
    const cJSON *af = cJSON_GetObjectItemCaseSensitive(a, "affects");
    if (cJSON_IsArray(af)) {
        int bits = 0;
        const cJSON *e;
        cJSON_ArrayForEach(e, af) {
            if (cJSON_IsString(e) && !strcmp(e->valuestring, "story")) bits |= CAMPAIGN_AFFECTS_STORY;
            else if (cJSON_IsString(e) && !strcmp(e->valuestring, "multiplayer")) bits |= CAMPAIGN_AFFECTS_MULTIPLAYER;
            else return fail("\"affects\" holds \"story\" and/or \"multiplayer\"");
        }
        d.camp.affects = bits;
    }
    if (argHas(a, "unlock")) {
        std::string u = argStr(a, "unlock");
        if (u == "open") d.camp.unlock = CAMPAIGN_UNLOCK_OPEN;
        else if (u == "ordered") d.camp.unlock = CAMPAIGN_UNLOCK_ORDERED;
        else return fail("\"unlock\" is \"ordered\" or \"open\"");
    }
    if (argHas(a, "name") || argHas(a, "author") || argHas(a, "description") || argHas(a, "affects") || argHas(a, "unlock")) d.modDirty = true;
    if (argHas(a, "mission")) {   // open one of its missions, by folder name or index
        int idx = -1;
        std::string m = argStr(a, "mission");
        for (int i = 0; i < d.camp.nmissions; i++) if (m == d.camp.missions[i]) idx = i;
        if (idx < 0) idx = argInt(a, "mission", -1);
        if (!d.modOpenMission(idx, err)) return fail(err);
    }
    return okJson(modJson(d));
}

// Publish / Deploy (the Mod > Publish... dialog's two buttons): the mod as a .pmod; saves and validates first.
static ToolResult modPackResult(bool ok, const std::string &path, uint64_t bytes, const std::string &err)
{
    if (!ok) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "path", path.c_str());
    cJSON_AddNumberToObject(o, "bytes", (double)bytes);
    return okJson(o);
}

ToolResult toolModPublish(Doc &d, const cJSON *a)
{
    std::string path, err;
    uint64_t bytes = 0;
    std::string out = argStr(a, "out_dir");
    if (out.empty()) out = d.modDir + "/dist";
    const bool ok = d.publishMod(out, path, bytes, err);
    return modPackResult(ok, path, bytes, err);
}

ToolResult toolModExtract(Doc &, const cJSON *a)
{
    const std::string pmod = argStr(a, "pmod"), dest = argStr(a, "dest");
    if (pmod.empty()) return fail("\"pmod\" is required: the package file X.pmod");
    std::string out, err;
    if (!Doc::extractMod(pmod, dest, out, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "dir", out.c_str());
    if (dest.empty()) cJSON_AddStringToObject(o, "old", (pmod + ".old").c_str());
    return okJson(o);
}

ToolResult toolModDeploy(Doc &d, const cJSON *)
{
    std::string path, err;
    uint64_t bytes = 0;
    const bool ok = d.deployMod(path, bytes, err);
    return modPackResult(ok, path, bytes, err);
}

ToolResult toolModSave(Doc &d, const cJSON *)
{
    std::string err;
    if (!d.saveMod(err)) return fail(err);
    std::vector<std::string> issues = d.validateMod();   // after the save: it made the id if it was missing
    cJSON *o = modJson(d);
    cJSON_AddStringToObject(o, "saved", (d.modDir + "/campaign.json").c_str());
    cJSON *w = cJSON_AddArrayToObject(o, "warnings");
    for (auto &i : issues) cJSON_AddItemToArray(w, cJSON_CreateString(i.c_str()));
    if (!d.modNotice.empty()) cJSON_AddItemToArray(w, cJSON_CreateString(d.modNotice.c_str()));
    return okJson(o);
}

ToolResult toolModAddMission(Doc &d, const cJSON *a)
{
    std::string err, folder;
    std::string level = argStr(a, "level", d.hasLevel ? d.level.name.c_str() : "bunker1");
    if (!d.modAddMission(argStr(a, "name", "New mission"), level, argStr(a, "folder"), folder, err)) return fail(err);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "folder", folder.c_str());
    cJSON_AddStringToObject(o, "mission", d.missionPath.c_str());
    cJSON_AddNumberToObject(o, "index", d.camp.nmissions - 1);
    return okJson(o);
}

ToolResult toolModReorder(Doc &d, const cJSON *a)
{
    std::string err;
    if (!d.hasMod) return fail("no mod is open (mod_open first)");
    const cJSON *order = cJSON_GetObjectItemCaseSensitive(a, "order");
    if (cJSON_IsArray(order)) {   // the full new order: a permutation of the folders
        std::vector<std::string> want;
        const cJSON *e;
        cJSON_ArrayForEach(e, order) if (cJSON_IsString(e)) want.push_back(e->valuestring);
        if ((int)want.size() != d.camp.nmissions) return fail("\"order\" must list every mission folder once");
        for (int i = 0; i < (int)want.size(); i++) {
            int from = -1;
            for (int k = i; k < d.camp.nmissions; k++) if (want[(size_t)i] == d.camp.missions[k]) from = k;
            if (from < 0) return fail("\"order\" must list every mission folder once (unknown or repeated: " + want[(size_t)i] + ")");
            d.modMoveMission(from, i, err);
        }
    } else {
        int idx = -1;
        std::string m = argStr(a, "mission");
        for (int i = 0; i < d.camp.nmissions; i++) if (m == d.camp.missions[i]) idx = i;
        if (idx < 0) idx = argInt(a, "mission", -1);
        if (!argHas(a, "to")) return fail("give \"order\": [folder, ...] or \"mission\" (folder name or index) with \"to\" (new index)");
        if (!d.modMoveMission(idx, argInt(a, "to", 0), err)) return fail(err);
    }
    return okJson(modJson(d));
}

// ---- textures: the decor's textures, the mod's replacements (textures/<n>.png) and the texture pack ----

ToolResult toolTextures(Doc &d, const cJSON *)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "texture_pack", d.texPack.empty() ? "ge007.passets" : d.texPack.c_str());
    cJSON_AddStringToObject(o, "texture_packs_dir", d.texturePacksRoot().c_str());
    if (!d.texSet.note().empty()) cJSON_AddStringToObject(o, "texture_pack_note", d.texSet.note().c_str());
    {
        d.rescanPacks();
        cJSON *pa = cJSON_AddArrayToObject(o, "packs");
        for (const PackChoice &c : d.packChoices) {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "file", c.file.c_str());
            cJSON_AddStringToObject(p, "name", c.name.c_str());
            if (!c.author.empty()) cJSON_AddStringToObject(p, "author", c.author.c_str());
            cJSON_AddBoolToObject(p, "usable", c.usable());
            if (!c.usable()) cJSON_AddStringToObject(p, "reason", c.reason.c_str());
            cJSON_AddItemToArray(pa, p);
        }
    }
    cJSON *mine = cJSON_AddArrayToObject(o, "mod_textures");
    for (int n : d.modTextures()) cJSON_AddItemToArray(mine, cJSON_CreateNumber(n));
    cJSON *arr = cJSON_AddArrayToObject(o, "level_textures");
    if (d.hasLevel && d.level.hasDecor) {
        const Decor &dc = d.level.decor;
        for (size_t i = 0; i < dc.tex.size(); i++) {
            const DecorTex &t = dc.tex[i];
            std::set<int> rooms;
            for (const DecorPrim &p : dc.prims)
                if (p.material >= 0 && dc.materials[(size_t)p.material].tex == (int)i && !p.uv.empty()) rooms.insert(p.room);
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "texnum", t.texnum);
            cJSON_AddNumberToObject(e, "w", t.w);
            cJSON_AddNumberToObject(e, "h", t.h);
            if (t.source >= 0) { cJSON_AddNumberToObject(e, "orig_w", t.origW); cJSON_AddNumberToObject(e, "orig_h", t.origH); }
            if (!t.key.empty()) cJSON_AddStringToObject(e, "key", t.key.c_str());
            cJSON_AddStringToObject(e, "from", t.source < 0 ? "level" : t.source == 0 ? "mod" : "pack");
            cJSON *ra = cJSON_AddArrayToObject(e, "rooms");
            for (int r : rooms) cJSON_AddItemToArray(ra, cJSON_CreateNumber(r));
            cJSON_AddItemToArray(arr, e);
        }
        cJSON *w = cJSON_AddArrayToObject(o, "warnings");
        for (const std::string &s : dc.texWarnings) cJSON_AddItemToArray(w, cJSON_CreateString(s.c_str()));
    } else {
        cJSON_AddStringToObject(o, "note", d.hasLevel ? "the level has no glTF decor: its textures are not listed" : "no level is open");
    }
    return okJson(o);
}

ToolResult toolModSetTexture(Doc &d, const cJSON *a)
{
    std::string err;
    if (!argHas(a, "texnum")) return fail("\"texnum\" (0..4095) is required: the texture number (textures lists the level's)");
    const int n = argInt(a, "texnum", -1);
    if (argHas(a, "remove") && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a, "remove"))) {
        if (!d.modRemoveTexture(n, err)) return fail(err);
    } else {
        const std::string png = argStr(a, "png");
        if (png.empty()) return fail("\"png\" is required: a PNG of your own (8-bit, not interlaced, any size); or \"remove\": true");
        if (!d.modSetTexture(n, png, err)) return fail(err);
    }
    return toolTextures(d, a);
}

ToolResult toolSetTexturePack(Doc &d, const cJSON *a)
{
    const std::string f = argStr(a, "file");
    if (!f.empty() && f != "ge007.passets" && f != "ge007.ptexpack") {
        d.rescanPacks();
        const PackChoice *found = nullptr;
        for (const PackChoice &c : d.packChoices) if (f == c.file) found = &c;
        if (!found) return fail("no texture pack " + f + " in " + d.texturePacksRoot() + " (textures lists them)");
        if (!found->usable()) return fail("texture pack " + f + " cannot be used: " + found->reason + " (a pack must have every texture of ge007.passets)");
    }
    d.texPack = (f == "ge007.passets" || f == "ge007.ptexpack") ? "" : f;
    d.syncModTextures();
    return toolTextures(d, a);
}

// ---- environment ----

bool argRgb(const cJSON *a, const char *k, float out[3])
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, k);
    if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) < 3) return false;
    for (int i = 0; i < 3; i++) out[i] = (float)cJSON_GetArrayItem(v, i)->valuedouble;
    return true;
}

bool argFloat(const cJSON *a, const char *k, float &out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, k);
    if (!cJSON_IsNumber(v)) return false;
    out = (float)v->valuedouble;
    return true;
}

ToolResult toolSetEnvironment(Doc &d, const cJSON *a)
{
    if (!d.hasMission) return fail("no mission is open");
    MissionEnv e = d.mission.env;
    // "clear": ["sun", "sky", ...] or "all": back to the level's own
    const cJSON *clr = cJSON_GetObjectItemCaseSensitive(a, "clear");
    if (cJSON_IsString(clr) && !strcmp(clr->valuestring, "all")) { memset(&e, 0, sizeof e); e.mood = MISSION_MOOD_KEEP; }
    else if (cJSON_IsArray(clr)) {
        const cJSON *c;
        cJSON_ArrayForEach(c, clr) {
            std::string n = cJSON_IsString(c) ? c->valuestring : "";
            if (n == "mood") e.mood = MISSION_MOOD_KEEP;
            else if (n == "sun") e.has_sun = 0;
            else if (n == "sky") e.has_sky = 0;
            else if (n == "clouds") e.has_clouds = 0;
            else if (n == "cloud_speed") e.has_cloud_speed = 0;
            else if (n == "sea") { e.has_sea = e.has_sea_height = e.has_sea_color = e.has_swell = e.has_chop = e.has_speed = 0; }
            else return fail("clear: unknown field '" + n + "' (mood, sun, sky, clouds, cloud_speed, sea, or \"all\")");
        }
    }
    if (argHas(a, "mood")) {
        std::string m = argStr(a, "mood");
        if (m == "day") e.mood = MISSION_MOOD_DAY;
        else if (m == "dusk") e.mood = MISSION_MOOD_DUSK;
        else if (m == "night") e.mood = MISSION_MOOD_NIGHT;
        else if (m == "keep" || m == "level") e.mood = MISSION_MOOD_KEEP;
        else return fail("mood is day, dusk, night or keep");
    }
    const cJSON *sun = cJSON_GetObjectItemCaseSensitive(a, "sun");
    if (cJSON_IsObject(sun)) {
        if (!e.has_sun) { e.has_sun = 1; e.sun_azimuth = 0; e.sun_elevation = 30; e.sun_color[0] = e.sun_color[1] = e.sun_color[2] = 1; e.sun_glow = 0.4f; }
        argFloat(sun, "azimuth", e.sun_azimuth); argFloat(sun, "elevation", e.sun_elevation);
        argRgb(sun, "color", e.sun_color); argFloat(sun, "glow", e.sun_glow);
    }
    if (argRgb(a, "sky", e.sky)) e.has_sky = 1;
    if (argRgb(a, "clouds", e.clouds)) e.has_clouds = 1;
    if (argFloat(a, "cloud_speed", e.cloud_speed)) e.has_cloud_speed = 1;
    const cJSON *sea = cJSON_GetObjectItemCaseSensitive(a, "sea");
    if (cJSON_IsObject(sea)) {
        if (!e.has_sea) { e.has_sea = 1; e.sea_enabled = 1; }
        const cJSON *en = cJSON_GetObjectItemCaseSensitive(sea, "enabled");
        if (cJSON_IsBool(en)) e.sea_enabled = cJSON_IsTrue(en);
        if (argFloat(sea, "height", e.sea_height)) e.has_sea_height = 1;
        if (argRgb(sea, "color", e.sea_color)) e.has_sea_color = 1;
        if (argFloat(sea, "swell", e.swell)) e.has_swell = 1;
        if (argFloat(sea, "chop", e.chop)) e.has_chop = 1;
        if (argFloat(sea, "speed", e.speed)) e.has_speed = 1;
    }
    if (!d.setEnv(e, true)) return fail("cannot set the environment");
    char *js = missionToJson(&d.mission);
    cJSON *full = js ? cJSON_Parse(js) : nullptr;
    free(js);
    cJSON *o = cJSON_CreateObject();
    const cJSON *env = full ? cJSON_GetObjectItemCaseSensitive(full, "environment") : nullptr;
    cJSON_AddItemToObject(o, "environment", env ? cJSON_Duplicate(env, 1) : cJSON_CreateObject());
    cJSON_AddStringToObject(o, "note", "the editor does not draw sky or sea yet: preview it in the game (test_run)");
    cJSON_Delete(full);
    return okJson(o);
}
