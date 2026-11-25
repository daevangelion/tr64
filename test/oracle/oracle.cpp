/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h and src/fixed/common.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
/* Host-test scaffolding reference: tools/n64_magma/test_caves_worldsim.cpp at the same commit.
 * Full BSD notice also in test/oracle/LICENSE-OPENLARA. */
#include "game.h"
#include <errno.h>
#include <ctype.h>
#include <string>
#include <vector>
#include "coop-input.h"

static_assert(sizeof(void*) == 4 && sizeof(int32) == 4 && sizeof(int16) == 2,
              "oracle requires ILP32");
static_assert((-1 >> 1) == -1, "arithmetic signed shift required");
static_assert(SLOT_MAX == 26, "schema 3 requires the complete 26-slot inventory");
static_assert(sizeof(((Inventory*)0)->options)/sizeof(((Inventory*)0)->options[0]) == 32,
              "schema 3 requires the complete 32-option inventory");

uint32 oracleLevelSize;
const void *TITLE_SCR = NULL;
static std::vector<uint8> levelBytes[LVL_MAX];
static std::vector<uint16> authoredItemFlags[LVL_MAX];
static std::string levelDirectory, initialLevelPath;
static int32 track = -1;
static LevelID initialLevel=LVL_MAX;
static SaveGame savedGame;
static std::vector<uint8> savedPayload;
static bool haveSave=false;
static unsigned currentTick=0;
static void fail(const char *message) { fprintf(stderr, "%s\n", message); exit(2); }
static void readLevelFile(const std::string &path, std::vector<uint8> &bytes) {
    FILE *file=fopen(path.c_str(),"rb");
    if (!file) { fprintf(stderr,"cannot open source PC level: %s\n",path.c_str()); exit(2); }
    if (fseek(file,0,SEEK_END)) fail("cannot seek source PC level");
    long size=ftell(file);
    if (size<4 || size>32*1024*1024 || fseek(file,0,SEEK_SET)) fail("invalid source PC level size");
    bytes.resize(size);
    if (fread(bytes.data(),1,size,file)!=size_t(size) || fclose(file)) fail("cannot read source PC level");
    if (bytes[0]!=0x20 || bytes[1] || bytes[2] || bytes[3])
        fail("unsupported level: fixed oracle accepts PC PHD version 0x20 only");
}
static uint32 saveFingerprint() {
    uint32 hash=2166136261u;
    const uint8 *header=reinterpret_cast<const uint8*>(&savedGame);
    for (size_t i=0;i<sizeof(savedGame);i++) hash=(hash^header[i])*16777619u;
    for (size_t i=0;i<savedPayload.size();i++) hash=(hash^savedPayload[i])*16777619u;
    return hash;
}
static void writeSaveEnvelope(const char *path) {
    if (!haveSave) return;
    uint8 bytes[SAVEGAME_SIZE];
    uint32 count=0;
    if (!saveEnvelopeSerializeV3(savedGame,savedPayload.data(),sizeof(gSaveData),
            bytes,sizeof(bytes),count))
        fail("cannot encode stored source save envelope");
    FILE *file=fopen(path,"wb");
    if (!file) fail("cannot open source save output descriptor");
    if (fwrite(bytes,1,count,file)!=count || fclose(file)) fail("cannot write source save envelope");
}

/* Host services. Save storage owns real source header/payload bytes; rendering
 * remains output-only. Never substitute game collision, camera, math or RNG. */
void renderInit() {}
void renderFree() {}
void renderLevelInit() {}
void renderLevelFree() {}
void renderBackground(const void*) {}
void* copyBackground() { return NULL; }
void osSetPalette(const uint16*) {}
void renderMesh(const Mesh*) {}
void renderShadow(int32, int32, int32, int32) {}
void renderSprite(int32, int32, int32, int32, int32) {}
int32 sphereIsVisible_c(int32, int32, int32, int32) { return 0; }
void osJoyVibrate(int32, int32, int32) {}
int32 osGetSystemTimeMS() { return 1; } // startLevel seeds logic=3, draw=7
bool osLoadSettings() { return false; }
bool osSaveSettings() { return false; }
bool osCheckSave() { return haveSave; }
bool osLoadGame(SaveGame &save, uint8 *data, uint32 capacity) {
    if (!haveSave || (savedGame.dataSize && !data) ||
        !saveEnvelopeIsValidV3(savedGame,uint32(sizeof(savedGame)+savedPayload.size()),capacity))
        return false;
    memcpy(&save,&savedGame,sizeof(save));
    if (savedGame.dataSize) memcpy(data,savedPayload.data(),savedGame.dataSize);
    return true;
}
bool osSaveGame(const SaveGame &save, const uint8 *data) {
    if ((save.dataSize && !data) ||
        save.level>=LVL_LOAD || save.level!=gLevelID ||
        save.itemCount!=level.itemsCount || (save.itemCount && !level.itemsInfo) ||
        !saveEnvelopeIsValidV3(save,uint32(sizeof(save))+save.dataSize,sizeof(gSaveData)))
        return false;
    std::vector<uint16> &flags=authoredItemFlags[save.level];
    if (!flags.empty() && flags.size()!=save.itemCount) return false;
    if (flags.empty()) {
        flags.reserve(save.itemCount);
        for (uint16 i=0;i<save.itemCount;i++) flags.push_back(level.itemsInfo[i].flags);
    }
    savedPayload.reserve(sizeof(gSaveData));
    if (save.dataSize) savedPayload.assign(data,data+save.dataSize);
    else savedPayload.clear();
    memcpy(&savedGame,&save,sizeof(save));
    haveSave=true;
    fprintf(stderr,"save-report: tick=%u bytes=%u fnv1a=%08x\n",currentTick,
        uint32(sizeof(savedGame)+savedPayload.size()),saveFingerprint());
    return true;
}
bool oracleAuthoredItemFlags(LevelID id, uint16 itemIndex, uint16 &flags) {
    if (!haveSave || int(id)<0 || id>=LVL_LOAD || id!=savedGame.level ||
        authoredItemFlags[id].size()!=savedGame.itemCount ||
        itemIndex>=authoredItemFlags[id].size())
        return false;
    flags=authoredItemFlags[id][itemIndex];
    return true;
}
const void* osLoadScreen(LevelID) { return NULL; }
const void* osLoadLevel(LevelID id) {
    if (int(id)<0 || id>=LVL_LOAD || !gLevelInfo[id].data) {
        fprintf(stderr,"unsupported source level id: %d\n",int(id));
        exit(2);
    }
    std::vector<uint8> &bytes=levelBytes[id];
    if (bytes.empty()) {
        std::string name=static_cast<const char*>(gLevelInfo[id].data);
        for (size_t i=0;i<name.size();i++) name[i]=char(tolower(static_cast<unsigned char>(name[i])));
        readLevelFile(id==initialLevel ? initialLevelPath : levelDirectory+name+".phd",bytes);
    }
    oracleLevelSize=uint32(bytes.size());
    return bytes.data();
}
void sndStopSample(int32) {}
void sndInitSamples() {}
void sndFreeSamples() {}
void* sndPlaySample(int32, int32, int32, int32) { return NULL; }
void sndPlayTrack(int32 id) { track = gCurTrack = id; }
bool sndTrackIsPlaying() { return track >= 0; }
void sndStopTrack() { track = gCurTrack = -1; }
void sndStop() { sndStopTrack(); }

static int roomIndex(const Room *room) { return room ? int(room - rooms) : -1; }
static void number(std::string &s, int value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    s += buffer;
}
static void unsignedNumber(std::string &s, uint32 value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%u", value);
    s += buffer;
}
/* Schema 4 `world`: FNV-1a-32 (byte stream, little-endian 16-bit values) over, in this order,
 *   sectors: for every logical room r < roomsCount: the room's bound RoomInfo index (u16), then
 *            for each of xSectors*zSectors sectors of room.sectors: floorIndex u16, boxIndex u16,
 *            roomBelow u8, floor u8, roomAbove u8, ceiling u8 (the live, possibly door-closed,
 *            block-lowered or flipped sector);
 *   boxes:   for every box b < boxesCount: overlap u16 (live blocked/blockable bits);
 *   flips:   for every of the 10 flip maps: mask u8, flags u8 (gFlipMapState).
 * Output is [sectors,boxes,flips,pairs] as unsigned decimals; `pairs` is 1 when the source's own
 * world validator worldBuildRoomPairs (object.h) accepts the world, 0 when it refuses (the check
 * that refuses every door update and map flip). Doors, push blocks and flips change the hashes. */
static uint32 hashByte(uint32 h, uint8 b) { return (h^b)*16777619u; }
static uint32 hashWord(uint32 h, uint16 v) { return hashByte(hashByte(h,uint8(v&255)),uint8(v>>8)); }
static void worldHashes(uint32 out[4]) {
    uint32 h=2166136261u;
    for (int r=0;r<level.roomsCount;r++) {
        const Room &room=rooms[r];
        h=hashWord(h,uint16(room.info-level.roomsInfo));
        const int count=int(room.info->xSectors)*int(room.info->zSectors);
        for (int k=0;k<count;k++) {
            const Sector &sector=room.sectors[k];
            h=hashWord(h,sector.floorIndex); h=hashWord(h,sector.boxIndex);
            h=hashByte(h,sector.roomBelow); h=hashByte(h,uint8(sector.floor));
            h=hashByte(h,sector.roomAbove); h=hashByte(h,uint8(sector.ceiling));
        }
    }
    out[0]=h; h=2166136261u;
    for (int b=0;b<level.boxesCount;b++) h=hashWord(h,level.boxes[b].overlap);
    out[1]=h; h=2166136261u;
    for (int i=0;i<TR1_FLIPMAP_COUNT;i++) { h=hashByte(h,gFlipMapState[i].mask); h=hashByte(h,gFlipMapState[i].flags); }
    out[2]=h;
    uint8 mate[MAX_ROOMS], alternate[MAX_ROOMS];
    out[3]=worldBuildRoomPairs(mate,alternate) ? 1 : 0;
}
static void vector(std::string &s, const vec3i &v) {
    s += '['; number(s,v.x); s += ','; number(s,v.y); s += ','; number(s,v.z); s += ']';
}
static void rotation(std::string &s, const vec3s &v) {
    s += '['; number(s,v.x); s += ','; number(s,v.y); s += ','; number(s,v.z); s += ']';
}
static void field(std::string &s, const char *name, int value) {
    s += ",\""; s += name; s += "\":"; number(s,value);
}
static void unsignedField(std::string &s, const char *name, uint32 value) {
    char buffer[32]; snprintf(buffer,sizeof(buffer),"%u",value);
    s += ",\""; s += name; s += "\":"; s += buffer;
}
static int itemIndex(const ItemObj *item) { return item ? int(item-items) : -1; }
template<class T> static void array(std::string &s, const T *values, size_t count) {
    s += '[';
    for (size_t i=0;i<count;i++) { if (i) s += ','; number(s,int(values[i])); }
    s += ']';
}
static void navRecord(std::string &s, const Nav &nav) {
    s += "{\"cellsCount\":"; number(s,int(nav.cellsCount));
    unsignedField(s,"zoneType",nav.zoneType); unsignedField(s,"weight",nav.weight);
    unsignedField(s,"endBox",nav.endBox); unsignedField(s,"nextBox",nav.nextBox);
    unsignedField(s,"headBox",nav.headBox); unsignedField(s,"tailBox",nav.tailBox);
    field(s,"stepHeight",nav.stepHeight); field(s,"dropHeight",nav.dropHeight);
    field(s,"vSpeed",nav.vSpeed); unsignedField(s,"mask",nav.mask);
    s += ",\"pos\":"; vector(s,nav.pos); s += ",\"cells\":[";
    for (unsigned i=0;i<level.boxesCount;i++) {
        if (i) s += ',';
        const Nav::Cell &c=nav.cells[i];
        s += '['; number(s,c.boxIndex); s += ','; number(s,c.weight);
        s += ','; number(s,c.end); s += ','; number(s,c.next); s += ']';
    }
    s += "]}";
}
static void armRecord(std::string &s, const ExtraInfoLara::Arm &arm) {
    s += "{\"angle\":"; rotation(s,arm.angle);
    s += ",\"angleAim\":"; rotation(s,arm.angleAim);
    field(s,"anim",arm.animIndex); field(s,"frame",arm.frameIndex);
    field(s,"target",itemIndex(arm.target)); field(s,"aim",arm.aim);
    field(s,"useBasis",arm.useBasis);
    s += ",\"flash\":["; number(s,arm.flash.timer); s += ','; number(s,arm.flash.angle);
    s += ','; number(s,arm.flash.offset); s += ','; number(s,arm.flash.intensity); s += "]}";
}
static void itemRecord(std::string &s,const ItemObj &item,unsigned schema) {
    s += "{\"index\":"; number(s,int(&item-items));
    field(s,"type",item.type); s += ",\"pos\":"; vector(s,item.pos);
    s += ",\"rot\":"; rotation(s,item.angle);
    field(s,"room",roomIndex(item.room)); field(s,"anim",item.animIndex);
    field(s,"frame",item.frameIndex); field(s,"state",item.state);
    field(s,"goal",item.goalState); field(s,"next",item.nextState);
    field(s,"flags",item.flags); field(s,"health",item.health);
    field(s,"hSpeed",item.hSpeed); field(s,"vSpeed",item.vSpeed);
    field(s,"turnSpeed",item.turnSpeed); field(s,"timer",item.timer);
    field(s,"water",item.waterState); field(s,"mood",item.mood);
    field(s,"headOffset",item.headOffset); field(s,"aggression",item.aggression);
    field(s,"intensity",item.intensity); field(s,"roomFloor",item.roomFloor);
    unsignedField(s,"hitMask",item.hitMask); unsignedField(s,"visibleMask",item.visibleMask);
    s += ",\"ai\":";
    const ExtraInfoEnemy *ai=NULL;
    for (int slot=0;slot<MAX_ENEMIES;slot++)
        if (item.extraE==enemiesExtra+slot && enemiesExtra[slot].enemy==&item)
            ai=enemiesExtra+slot;
    if (!ai) s += "null";
    else {
        s += "{\"slot\":"; number(s,int(ai-enemiesExtra));
        field(s,"rotHead",ai->rotHead); field(s,"rotNeck",ai->rotNeck);
        field(s,"maxTurn",ai->maxTurn);
        if (schema==5) {
            field(s,"target",itemIndex(ai->target));
            s += ",\"contact\":[";unsignedNumber(s,ai->contact[0]);s += ',';
            unsignedNumber(s,ai->contact[1]);s += ']';
        }
        s += ",\"nav\":"; navRecord(s,ai->nav); s += '}';
    }
    s += '}';
}
static void playerRecord(std::string &s,const Lara &lara,unsigned schema) {
    itemRecord(s,lara,schema); s.pop_back();
    field(s,"oxygen",lara.oxygen);
    const ExtraInfoLara &extra=*lara.extraL;
    field(s,"weaponState",extra.weaponState); field(s,"weapon",extra.weapon);
    field(s,"goalWeapon",extra.goalWeapon); field(s,"input",lara.input);
    field(s,"swimTimer",extra.swimTimer); field(s,"vSpeedHack",extra.vSpeedHack);
    field(s,"moveAngle",extra.moveAngle); field(s,"hitFrame",extra.hitFrame);
    field(s,"hitTimer",extra.hitTimer); field(s,"hitQuadrant",extra.hitQuadrant);
    field(s,"hazardCause",extra.hazardCause); field(s,"lastInput",extra.lastInput);
    field(s,"healthTimer",extra.healthTimer); field(s,"dozy",extra.dozy);
    s += ",\"head\":"; rotation(s,extra.head.angle);
    s += ",\"torso\":"; rotation(s,extra.torso.angle);
    s += ",\"armR\":"; armRecord(s,extra.armR);
    s += ",\"armL\":"; armRecord(s,extra.armL);
    s += ",\"meshes\":"; array(s,extra.meshes,JOINT_MAX);
    s += ",\"nav\":"; navRecord(s,extra.nav); s += '}';
}
static void cameraRecord(std::string &s,const Camera &camera) {
    s += "{\"pos\":"; vector(s,camera.view.pos);
    s += ",\"target\":"; vector(s,camera.target.pos);
    field(s,"room",roomIndex(camera.view.room));
    field(s,"targetRoom",roomIndex(camera.target.room));
    field(s,"targetDist",camera.targetDist);
    s += ",\"targetAngle\":"; rotation(s,camera.targetAngle);
    s += ",\"angle\":"; rotation(s,camera.angle);
    field(s,"laraItem",itemIndex(camera.laraItem)); field(s,"lastItem",itemIndex(camera.lastItem));
    field(s,"lookAtItem",itemIndex(camera.lookAtItem)); field(s,"speed",camera.speed);
    field(s,"timer",camera.timer); field(s,"index",camera.index);
    field(s,"lastIndex",camera.lastIndex); field(s,"bounce",camera.bounce);
    field(s,"mode",camera.mode); field(s,"lastFixed",camera.lastFixed); field(s,"center",camera.center);
    s += '}';
}
static void cinematicRecord(std::string &s) {
    const Camera &camera=gCinematicCamera;
    s += "{\"pos\":"; vector(s,camera.view.pos);
    field(s,"room",roomIndex(camera.view.room));
    field(s,"targetAngleY",camera.targetAngle.y);
    s += ",\"angle\":["; number(s,camera.angle.x); s += ','; number(s,camera.angle.y); s += ']';
    field(s,"timer",camera.timer);
    s += ",\"eye\":"; vector(s,gCameraViewPos);
    s += '}';
}
static void partyRecord(std::string &s) {
    unsigned present=0,alive=0;
    for (unsigned i=0;i<MAX_PLAYERS;++i)
        if (players[i]) {
            present|=1u<<i;
            if (players[i]->health>0)alive|=1u<<i;
        }
    field(s,"present",present);field(s,"alive",alive);
    s += ",\"players\":[";
    for (unsigned i=0;i<MAX_PLAYERS;++i) {
        if (i)s += ',';
        if (!players[i]) { s += "null";continue; }
        s += "{\"slot\":";number(s,i);
        s += ",\"lara\":";playerRecord(s,*players[i],5);
        s += ",\"camera\":";cameraRecord(s,playersExtra[i].camera);
        s += ",\"flow\":[";number(s,gFlowState[i].cameraIndex);s += ',';
        number(s,gFlowState[i].active);s += ']';
        const LaraHazardState &burn=gLaraHazardState[i];
        s += ",\"burn\":[";number(s,burn.burnTicks);s += ',';
        number(s,burn.damageCooldown);s += ',';number(s,burn.flameItemIndex);s += ',';
        number(s,burn.flags);s += ']';
        field(s,"interaction",itemIndex(gCoopInteraction[i]));
        s += '}';
    }
    s += "],\"cinematic\":";
    if (isCutsceneLevel())cinematicRecord(s);
    else s += "null";
    const int owner=players[1] && inventory.lara==players[1];
    field(s,"ringOwner",inventory.state==INV_STATE_NONE ? -1 : owner);
    field(s,"useOwner",inventory.useSlot==SLOT_MAX || !inventory.lara ? -1 : owner);
    field(s,"deathReady",inventory.page==INV_PAGE_DEATH && inventory.state==INV_STATE_SHOW &&
          inventory.frameIndex==inventory.frameTarget);
    field(s,"pendingLevel",gNextLevel);
    field(s,"completionOwed",gCoopRetireLevel);
    s += ",\"retiredActions\":[";
    bool retiredComma=false;
    for (unsigned i=0;i<MAX_PLAYERS;++i) {
        const CoopRetiredAction &a=gCoopRetired[i];
        if (!a.actor)continue;
        if (retiredComma)s += ',';
        retiredComma=true;
        s += '[';number(s,itemIndex(gCoopInteraction[i]));s += ',';
        number(s,itemIndex(a.actor));s += ',';number(s,a.ticks);s += ']';
    }
    s += ']';
    s += ",\"blocks\":[";
    bool comma=false;
    for (unsigned i=0;i<level.itemsCount;++i) {
        const ItemObj &b=items[i];
        if (b.type<ITEM_BLOCK_1 || b.type>ITEM_BLOCK_4 || !b.room)continue;
        if (comma)s += ',';
        comma=true;
        s += '[';number(s,i);s += ',';number(s,b.room-rooms);
        s += ",[";number(s,b.pos.x);s += ',';number(s,b.pos.y);s += ',';number(s,b.pos.z);
        s += "],";number(s,b.flags&ITEM_FLAG_STATUS);s += ',';
        number(s,b.room->getSector(b.pos.x,b.pos.z)->floor*256);s += ']';
    }
    s += ']';
    s += ",\"effect\":[";number(s,gEffectState.id);s += ',';number(s,gEffectState.timer);
    s += ',';number(s,itemIndex(gEffectState.lara));s += ',';number(s,gEffectState.cameraBounce);
    s += "],\"pickups\":[";
    for (unsigned i=0;i<gCoopPickupCount;++i) {
        if (i)s += ',';
        s += '[';number(s,gCoopPickups[i]&0xff);s += ',';number(s,gCoopPickups[i]>>8);s += ']';
    }
    s += ']';
}
static void emit(FILE *out,int tick,unsigned schema) {
    std::string s="{\"schema\":";number(s,int(schema));s += ",\"tick\":";number(s,tick);
    if (schema>=3)field(s,"level",gLevelID);
    if (schema==5)partyRecord(s);
    else {
        s += ",\"lara\":";
        if (players[0])playerRecord(s,*players[0],schema);
        else s += "null";
        s += ",\"camera\":";
        if (players[0])cameraRecord(s,players[0]->extraL->camera);
        else if (isCutsceneLevel())cinematicRecord(s);
        else s += "null";
    }
    s += ",\"active\":[";
    bool active[MAX_ITEMS] = {};
    bool first=true;
    for (const ItemObj *item=ItemObj::sFirstActive; item; item=item->nextActive) {
        if (item < items || item >= items+MAX_ITEMS || active[item-items])
            fail("invalid active item list");
        active[item-items]=true;
        if (!first) s += ',';
        first=false; itemRecord(s,*item,schema);
    }
    s += "],\"save\":{\"version\":";
    number(s,int(gSaveGame.version)); unsignedField(s,"dataSize",gSaveGame.dataSize);
    field(s,"itemCount",gSaveGame.itemCount); field(s,"level",gSaveGame.level);
    field(s,"track",gSaveGame.track); field(s,"secrets",gSaveGame.secrets);
    field(s,"pickups",gSaveGame.pickups); unsignedField(s,"time",gSaveGame.time);
    unsignedField(s,"distance",gSaveGame.distance);
    unsignedField(s,"randSeedLogic",gSaveGame.randSeedLogic);
    unsignedField(s,"randSeedDraw",gSaveGame.randSeedDraw);
    field(s,"mediUsed",gSaveGame.mediUsed); field(s,"ammoUsed",gSaveGame.ammoUsed);
    field(s,"kills",gSaveGame.kills); field(s,"flipped",gSaveGame.flipped);
    s += ",\"tracks\":"; array(s,gSaveGame.tracks,64);
    s += ",\"invSlots\":"; array(s,gSaveGame.invSlots,64);
    s += "},\"inventory\":{\"counts\":"; array(s,inventory.counts,SLOT_MAX);
    field(s,"numKeys",inventory.numKeys); field(s,"useSlot",inventory.useSlot);
    field(s,"state",inventory.state); field(s,"page",inventory.page);
    if (schema>=3) {
        field(s,"timer",inventory.timer); field(s,"nextState",inventory.nextState);
        field(s,"frameIndex",inventory.frameIndex); field(s,"frameTarget",inventory.frameTarget);
        field(s,"height",inventory.height); field(s,"radius",inventory.radius);
        field(s,"selDist",inventory.selDist); field(s,"pitch",inventory.pitch);
        field(s,"rot",inventory.rot); field(s,"rotItem",inventory.rotItem);
        field(s,"selRotPre",inventory.selRotPre); field(s,"selRotX",inventory.selRotX);
        field(s,"selRotY",inventory.selRotY);
        field(s,"heightTarget",inventory.heightTarget); field(s,"heightInc",inventory.heightInc);
        field(s,"radiusTarget",inventory.radiusTarget); field(s,"radiusInc",inventory.radiusInc);
        field(s,"pitchTarget",inventory.pitchTarget); field(s,"pitchInc",inventory.pitchInc);
        field(s,"rotTarget",inventory.rotTarget); field(s,"rotInc",inventory.rotInc);
        field(s,"rotItemInc",inventory.rotItemInc);
        field(s,"selDistTarget",inventory.selDistTarget); field(s,"selDistInc",inventory.selDistInc);
        field(s,"selRotPreTarget",inventory.selRotPreTarget); field(s,"selRotPreInc",inventory.selRotPreInc);
        field(s,"selRotXTarget",inventory.selRotXTarget); field(s,"selRotXInc",inventory.selRotXInc);
        field(s,"selRotYTarget",inventory.selRotYTarget); field(s,"selRotYInc",inventory.selRotYInc);
        s += ",\"itemsList\":"; array(s,inventory.itemsList,SLOT_MAX);
        field(s,"itemsCount",inventory.itemsCount); field(s,"itemIndex",inventory.itemIndex);
        field(s,"nextIndex",inventory.nextIndex); field(s,"optionIndex",inventory.optionIndex);
        field(s,"optionsCount",inventory.optionsCount); field(s,"optionsWidth",inventory.optionsWidth);
        field(s,"optionsHeight",inventory.optionsHeight); unsignedField(s,"itemVisMask",inventory.itemVisMask);
        field(s,"passportPage",inventory.passportPage); field(s,"statsNextLevel",inventory.statsNextLevel);
        s += ",\"options\":[";
        for (size_t i=0;i<sizeof(inventory.options)/sizeof(inventory.options[0]);i++) {
            if (i) s += ',';
            const Inventory::Option &option=inventory.options[i];
            s += '['; number(s,option.type); s += ','; number(s,option.str);
            s += ','; number(s,option.value); s += ']';
        }
        s += ']';
    }
    s += '}';
    if (schema>=3) {
        s += ",\"settings\":["; number(s,gSettings.video_gamma);
        s += ','; number(s,gSettings.video_fog); s += ','; number(s,gSettings.video_fps);
        s += ','; number(s,gSettings.video_vsync); s += ','; number(s,gSettings.audio_sfx);
        s += ','; number(s,gSettings.audio_music); s += ','; number(s,gSettings.controls_vibration);
        s += ','; number(s,gSettings.controls_swap); s += ']';
    }
    if (schema>=4) {
        s += ",\"world\":";
        if (gLevelID==LVL_TR1_TITLE) s += "null";
        else {
            uint32 w[4]; worldHashes(w);
            s += '['; unsignedNumber(s,w[0]); s += ','; unsignedNumber(s,w[1]);
            s += ','; unsignedNumber(s,w[2]); s += ','; unsignedNumber(s,w[3]); s += ']';
        }
    }
    char rng[96];
    snprintf(rng,sizeof(rng),",\"rng\":[%u,%u]}",uint32(gRandSeedLogic),uint32(gRandSeedDraw));
    s += rng;
    uint32 hash=2166136261u;
    for (size_t i=0;i<s.size();i++) hash=(hash^uint8(s[i]))*16777619u;
    s.pop_back();
    char suffix[48]; snprintf(suffix,sizeof(suffix),",\"hash\":\"%08x\"}\n",hash);
    s += suffix;
    if (fwrite(s.data(),1,s.size(),out)!=s.size()) fail("trace write failed");
}
static unsigned parse(const char *text) {
    int base=10;
    const char *digits=text;
    if (text[0]=='0' && (text[1]=='x' || text[1]=='X')) { base=16; digits+=2; }
    if (!*digits) fail("invalid integer argument");
    for (const char *p=digits;*p;p++)
        if (!((*p>='0' && *p<='9') || (base==16 && ((*p>='a' && *p<='f') || (*p>='A' && *p<='F')))))
            fail("integer must be decimal or 0x hexadecimal");
    errno=0; char *end=NULL; unsigned long value=strtoul(digits,&end,base);
    if (errno || end==digits || *end || value>0x7fffffffu) fail("invalid integer argument");
    return unsigned(value);
}
static int signedNumber(const char *text) {
    errno=0; char *end=NULL; long value=strtol(text,&end,10);
    if (errno || end==text || *end) fail("invalid signed start coordinate");
    return int(value);
}
static void saveLoadBoundary(unsigned tick, unsigned saveTick, unsigned loadTick) {
    if (tick==saveTick && !gameSave()) {
        fprintf(stderr,"source gameSave failed: tick=%u level=%d\n",tick,int(gLevelID));
        exit(2);
    }
    if (tick==loadTick && !gameLoad()) {
        fprintf(stderr,"source gameLoad failed: tick=%u level=%d\n",tick,int(gLevelID));
        exit(2);
    }
}
static void placePlayer(unsigned slot,const int32 *start) {
    if (!players[slot]) fail("start placement requires a gameplay Lara");
    if (start[0]<0 || start[0]>=level.roomsCount ||
        start[4]<-32768 || start[4]>32767) fail("invalid start room or angle");
    Room *room=rooms+start[0];
    const RoomInfo &info=*room->info;
    if (start[1]<(info.x<<8) || start[1]>=(info.x<<8)+info.xSectors*1024 ||
        start[3]<(info.z<<8) || start[3]>=(info.z<<8)+info.zSectors*1024)
        fail("start position outside named room bounds");
    if (room->getRoom(start[1],start[2],start[3])!=room)
        fail("start point resolves to a different room");
    const Sector *sector=room->getSector(start[1],start[3]);
    const int floor=sector->getFloor(start[1],start[2],start[3]);
    const int ceiling=sector->getCeiling(start[1],start[2],start[3]);
    if (floor==WALL || ceiling==WALL || start[2]>floor || start[2]<ceiling)
        fail("start position outside playable sector");
    resetLara(slot,start[0],_vec3i(start[1],start[2],start[3]),start[4]);
    players[slot]->updateWaterState();
    players[slot]->extraL->camera.init(players[slot]);
    coopRefreshPose(players[slot]);
}
static void coopCommands(const std::vector<CoopCommand> &commands,size_t &next,unsigned tick) {
    while (next<commands.size() && commands[next].tick==tick) {
        const CoopCommand &c=commands[next++];
        switch(c.kind) {
        case COOP_JOIN: if (!coopJoin())fail("co-op join refused");break;
        case COOP_LEAVE: if (coopLeave()!=0)fail("co-op leave refused");break;
        case COOP_JOIN_REFUSED: if (coopJoin())fail("co-op join unexpectedly accepted");break;
        case COOP_LEAVE_RESULT:
            if (coopLeave()!=c.value[0])fail("co-op leave returned an unexpected result");
            break;
        case COOP_SAVE_BLOCKED:
            if (int(coopSaveBlocked())!=c.value[0] || (c.value[0] && gameSave()))
                fail("co-op save preflight returned an unexpected result");
            break;
        case COOP_WARP_RESULT:
            if (coopWarp(c.slot)!=c.value[0])fail("co-op warp returned an unexpected result");
            break;
        case COOP_FLIP:
            worldRequestFlipToggle();
            if (!worldApplyPendingFlip())fail("co-op flip refused");
            break;
        case COOP_PIT: {
            const int32 *v=c.value;
            if (v[0]>=level.roomsCount || v[3]<=0 || v[3]>127 || !rooms[v[0]].modify())fail("co-op pit refused");
            Sector* s=const_cast<Sector*>(rooms[v[0]].getSector(v[1],v[2]));
            if (s->floor==NO_FLOOR || s->floor+v[3]>127)fail("co-op pit floor invalid");
            s->floor+=v[3];
            break;
        }
        case COOP_HEAVY: {
            const int32 *v=c.value;
            if (v[0]>=level.roomsCount)fail("co-op heavy refused");
            rooms[v[0]].getSector(v[1],v[3])->getFloor(v[1],v[2],v[3]);
            checkTrigger(gLastFloorData,NULL);
            break;
        }
        case COOP_WARP: (void)coopWarp(c.slot);break;
        case COOP_PLACE: placePlayer(c.slot,c.value);break;
        case COOP_HEALTH:
            if (!players[c.slot])fail("co-op health requires a present actor");
            players[c.slot]->health=int16(c.value[0]);
            break;
        case COOP_HIT:
            if (!players[c.slot] || players[c.slot]->health<=0 ||
                c.value[0]>=level.itemsCount ||
                !players[c.slot]->applyWeaponHit(items+c.value[0],c.value[1],items[c.value[0]].pos))
                fail("co-op hit refused");
            break;
        }
    }
}
int main(int argc, char **argv) {
    const char *levelPath=NULL,*inputPath=NULL,*outPath=NULL,*saveOutPath=NULL;
    unsigned ticks=0, traceSchema=2, saveTick=~0u, loadTick=~0u; bool haveTicks=false;
    for (int i=1;i<argc;i+=2) {
        if (i+1>=argc) fail("expected --level PHD --input MASKS --ticks N --out TRACE");
        if (!strcmp(argv[i],"--level")) levelPath=argv[i+1];
        else if (!strcmp(argv[i],"--input")) inputPath=argv[i+1];
        else if (!strcmp(argv[i],"--out")) outPath=argv[i+1];
        else if (!strcmp(argv[i],"--ticks")) { ticks=parse(argv[i+1]); haveTicks=true; }
        else if (!strcmp(argv[i],"--trace-schema")) traceSchema=parse(argv[i+1]);
        else if (!strcmp(argv[i],"--save-tick")) saveTick=parse(argv[i+1]);
        else if (!strcmp(argv[i],"--load-tick")) loadTick=parse(argv[i+1]);
        else if (!strcmp(argv[i],"--save-out")) saveOutPath=argv[i+1];
        else fail("unknown argument");
    }
    if (!levelPath || !inputPath || !outPath || !haveTicks || ticks>4096) fail("required arguments missing or scene exceeds 4096 ticks");
    if (traceSchema<2 || traceSchema>5) fail("--trace-schema must be 2, 3, 4 or 5");
    if ((saveTick!=~0u && saveTick>ticks) || (loadTick!=~0u && loadTick>ticks))
        fail("save/load tick must be within the scene");
    if (saveOutPath) {
        const char *fd=strncmp(saveOutPath,"/proc/self/fd/",14)==0 ? saveOutPath+14 :
                       strncmp(saveOutPath,"/dev/fd/",8)==0 ? saveOutPath+8 : NULL;
        if (!fd || !*fd || strspn(fd,"0123456789")!=strlen(fd))
            fail("--save-out requires an inherited /proc/self/fd/N or /dev/fd/N");
    }
    FILE *file;
    std::vector<std::pair<uint16,uint16> > masks;
    std::vector<CoopCommand> commands;
    bool firstToken=true,coop=false;
    file=fopen(inputPath,"r"); if (!file) fail("cannot open input");
    char token[128];
    bool haveStart=false;
    int start[5]={};
    // Test-only setup directives, all before the first mask: `give TYPE COUNT`
    // (the real inventory.addPickup(TYPE), COUNT times), `item INDEX HEALTH` and
    // `activate INDEX TIMER` (what an ACTIVATE_OBJECT trigger action does to the item).
    std::vector<std::pair<int,int> > gives, itemHealth, activates;
    while (fscanf(file,"%127s",token)==1) {
        if (firstToken && !strcmp(token,"coop")) {
            if (fscanf(file,"%127s",token)!=1 || strcmp(token,"1"))fail("co-op input version must be 1");
            firstToken=false;coop=true;continue;
        }
        firstToken=false;
        if (coop) {
            CoopCommand command={};
            const int parsed=coop_read_command(file,token,&command,parse,signedNumber);
            if (parsed) {
                if (parsed<0 || !masks.empty() || commands.size()==COOP_COMMAND_CAPACITY ||
                    command.tick>ticks || (!commands.empty() && command.tick<commands.back().tick))
                    fail("co-op commands must be valid and ordered before all masks");
                commands.push_back(command);continue;
            }
        }
        bool isGive=!strcmp(token,"give"), isActivate=!strcmp(token,"activate");
        if (isGive || isActivate || !strcmp(token,"item")) {
            if (!masks.empty()) fail("give/item/activate must appear before all masks");
            int pair[2];
            for (int i=0;i<2;i++) {
                if (fscanf(file,"%127s",token)!=1) fail("give/item/activate requires two integers");
                pair[i]=signedNumber(token);
            }
            (isGive ? gives : isActivate ? activates : itemHealth).push_back(std::make_pair(pair[0],pair[1]));
            continue;
        }
        if (!strcmp(token,"start")) {
            if (haveStart || !masks.empty()) fail("start must appear once before all masks");
            for (int i=0;i<5;i++) {
                if (fscanf(file,"%127s",token)!=1) fail("start requires room x y z angle");
                start[i]=signedNumber(token);
            }
            haveStart=true;
            continue;
        }
        uint32 mask=parse(token);
        if (mask & ~0xffeu) fail("input has undefined bits");
        if (mask & IN_START) fail("START/free camera is unsupported by this oracle");
        uint32 second=0;
        if (coop) {
            if (fscanf(file,"%127s",token)!=1)fail("co-op input missing second mask");
            second=parse(token);
            if (second&~0xbfeu)fail("co-op input has undefined bits or START");
        }
        if (masks.size()>=ticks)fail("too many input masks");
        masks.emplace_back(uint16(mask),uint16(second));
    }
    if (ferror(file) || fclose(file) || masks.size()!=ticks) fail("input must contain exactly N masks");
    const char *base=strrchr(levelPath,'/'); base=base?base+1:levelPath;
    std::string stem(base); size_t dot=stem.find_last_of('.'); if (dot!=std::string::npos) stem.resize(dot);
    bool found=false;
    for (int id=LVL_TR1_TITLE;id<int(LVL_LOAD);id++) {
        const char *name=static_cast<const char*>(gLevelInfo[id].data);
        if (name && strcasecmp(name,stem.c_str())==0) { gLevelID=LevelID(id); found=true; break; }
    }
    if (!found) fail("level filename must name TITLE, a gameplay level or a cutscene level");
    // A cutscene level, or the level whose successor is a cutscene (3B, 7B, 10A, 10B), may run
    // up to 4096 ticks so a scene can cross the transition and play the cinematic out. LEVEL4 and
    // LEVEL8B may too (tr64 hunks 26 and 17): four keys into four keyholes, and four switches, four
    // bridges and four artifacts, do not fit 600 ticks.
    {
        const int next=int(gLevelID)+1;
        const bool leadsToCutscene=next==int(LVL_TR1_CUT_1) || next==int(LVL_TR1_CUT_2) ||
                                   next==int(LVL_TR1_CUT_3) || next==int(LVL_TR1_CUT_4);
        const bool longScene=gLevelID==LVL_TR1_4 || gLevelID==LVL_TR1_8B;
        if (ticks>600 && !isCutsceneLevel() && !leadsToCutscene && !longScene) fail("scene exceeds 600 ticks");
    }
    initialLevel=gLevelID;
    initialLevelPath=levelPath;
    const char *slash=strrchr(levelPath,'/');
    levelDirectory=slash ? std::string(levelPath,size_t(slash-levelPath+1)) : std::string();
    gameInit();
    if (!players[0] && gLevelID!=LVL_TR1_TITLE && !isCutsceneLevel()) fail("level has no Lara start");
    if (haveStart)placePlayer(0,start);
    // Placed-state setup (test only): same order and semantics as the port's host driver.
    for (size_t i=0;i<gives.size();i++) {
        if (!players[0]) fail("give requires a gameplay Lara");
        if (gives[i].first<0 || gives[i].first>=int(ITEM_MAX) || gives[i].second<1 || gives[i].second>1000)
            fail("give: type or count out of range");
        for (int n=0;n<gives[i].second;n++)
            if (!inventory.addPickup(ItemType(gives[i].first))) fail("give: addPickup refused the item");
    }
    for (size_t i=0;i<itemHealth.size();i++) {
        if (itemHealth[i].first<0 || itemHealth[i].first>=int(level.itemsCount) ||
            itemHealth[i].second<-32768 || itemHealth[i].second>32767)
            fail("item: index or health out of range");
        items[itemHealth[i].first].health=int16(itemHealth[i].second);
    }
    for (size_t i=0;i<activates.size();i++) {
        if (activates[i].first<0 || activates[i].first>=int(level.itemsCount) || activates[i].second<0 || activates[i].second>127)
            fail("activate: index or timer out of range");
        // The ACTIVATE_OBJECT trigger action (room.h) with every mask bit set and the timer field TIMER.
        ItemObj &item=items[activates[i].first];
        if (item.flags & ITEM_FLAG_ONCE) fail("activate: item already used once");
        item.timer=activates[i].second;
        if (item.timer!=1) item.timer*=30;
        item.flags|=ITEM_FLAG_MASK;
        if (!(item.flags & ITEM_FLAG_ACTIVE)) {
            worldTriggerActivate(&item);                            // the trigger's own routine (hunk 28)
        }
    }
    file=fopen(outPath,"wb"); if (!file) fail("cannot open trace");
    size_t nextCommand=0;
    coopCommands(commands,nextCommand,0);
    saveLoadBoundary(0,saveTick,loadTick);
    emit(file,0,traceSchema);
    for (unsigned tick=1;tick<=ticks;tick++) {
        currentTick=tick;
        coopCommands(commands,nextCommand,tick);
        keys=masks[tick-1].first;
        gCoopInput=masks[tick-1].second;
        gameUpdate(1);
        saveLoadBoundary(tick,saveTick,loadTick);
        emit(file,int(tick),traceSchema);
    }
    if (saveOutPath) writeSaveEnvelope(saveOutPath);
    if (fclose(file)) fail("cannot close trace");
    if (const Lara *lara=players[0])
        printf("trace ticks=%u records=%u level=%s lara=%d,%d,%d health=%d water=%u\n",ticks,ticks+1,base,lara->pos.x,lara->pos.y,lara->pos.z,lara->health,lara->waterState);
    else
        printf("trace ticks=%u records=%u level=%s lara=null\n",ticks,ticks+1,base);
    return 0;
}
