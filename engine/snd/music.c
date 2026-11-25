/* SPDX-License-Identifier: MIT */
#include "engine/snd/music.h"
#include "engine/snd/music_decode.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/game.h"
#include "tr64/io.h"
#include <string.h>
#define TRACKS 56u
#define INDEX_CAPACITY 600u
#define WINDOW_BYTES 4096u
#define WINDOW_COUNT 3u
#define NONE UINT32_MAX
#define WINDOW_NONE 0xffffu
enum { STOPPED, DRAIN, QUERY, HEADER, PRIME, PLAY };
/* Windows 0/1 are the data[] pair; window 2 is the loop window in the index. */
typedef struct {
    tr64_io_req_t request;
    uint16_t block;
    uint8_t pending,valid;
} music_window_t;
typedef struct { uint32_t rom_offset,raw_crc32; } music_block_t;
/* Query needs full 16 B records. Playback needs only ROM/CRC because the checked
 * native partition is regular 4 KiB. Reuse the released half for the loop window. */
typedef union {
    tr64_pack_stream_block_t full[INDEX_CAPACITY];
    struct {
        music_block_t blocks[INDEX_CAPACITY];
        uint8_t loop[WINDOW_BYTES] __attribute__((aligned(16)));
    } compact;
} music_index_t;
typedef struct {
    tr64_pack_stream_source_t catalog[TRACKS];
    music_index_t index;
    tr64_pack_stream_query_t query;
    uint8_t data[2][WINDOW_BYTES] __attribute__((aligned(16)));
    music_window_t windows[WINDOW_COUNT];
    /* Byte flags sit in the padding before the 16-byte-aligned output. */
    uint8_t phase,active,bound,have_serial,compacted;
    int16_t output[TR64_MUSIC_OUTPUT_FRAMES] __attribute__((aligned(16)));
    tr64_music_decoder_t decoder;
    tr64_music_format_t format;
    uint32_t pack_bytes,block_count,chunk_bytes,serial;
    uint32_t position,fraction,begin_tick;
    uint32_t unit,unit_sample,play_block,within;
    int32_t desired,current,ambient;
    uint32_t starts,loops,unavailable,underruns,frames,reads,read_bytes;
    uint32_t peak_ticks,startup_peak_ticks;
    uint32_t frame_ticks,frame_peak_ticks;
    uint64_t mix_us;
} music_t;
static music_t *m;
uint32_t tr64_music_memory_bytes(void) { return sizeof(music_t); }
void tr64_music_init(void *memory,uint32_t bytes)
{
    uint32_t i;
    if(m || !memory || ((uintptr_t)memory&15u) || bytes!=sizeof(music_t))
        sys_error("music boot memory contract");
    m=memory;memset(m,0,sizeof(*m));m->desired=m->current=m->ambient=-1;
    for(i=0;i<WINDOW_COUNT;++i)m->windows[i].block=WINDOW_NONE;
    log_printf("music-boot: bytes=%lu index_capacity=%u windows=%u window_bytes=%u output_frames=%u\n",
               (unsigned long)bytes,INDEX_CAPACITY,WINDOW_COUNT,WINDOW_BYTES,TR64_MUSIC_OUTPUT_FRAMES);
}
void tr64_music_catalog_bind(const tr64_pack_t *pack)
{
    uint32_t i;char name[]="MUSIC_00";
    if(!m || m->active || m->phase!=STOPPED)sys_error("music bind while active");
    memset(m->catalog,0,sizeof(m->catalog));
    for(i=0;i<TRACKS;++i) {
        name[6]=(char)('0'+(i+2)/10);name[7]=(char)('0'+(i+2)%10);
        (void)tr64_pack_stream_source(pack,name,TR64_MUSIC_KIND,m->catalog+i);
    }
    m->pack_bytes=pack->bytes;m->bound=1;
}
static uint8_t *window_data(const music_window_t *w)
{
    uint32_t i=(uint32_t)(w-m->windows);
    return i<2 ? m->data[i] : m->index.compact.loop;
}
/* Data blocks only (>=1) after the native partition was checked. */
static uint32_t block_bytes(uint32_t block)
{
    uint32_t bytes=m->format.total-m->format.data_offset-(block-1)*m->chunk_bytes;
    return bytes>m->chunk_bytes ? m->chunk_bytes : bytes;
}
static tr64_pack_stream_block_t block_info(uint32_t block)
{
    tr64_pack_stream_block_t result;
    if(block>=m->block_count)sys_error("music invalid stream block");
    if(!m->compacted)return m->index.full[block];
    result.logical_offset=block ? m->format.data_offset+(block-1)*m->chunk_bytes : 0;
    result.raw_bytes=block ? block_bytes(block) : m->format.data_offset;
    result.rom_offset=m->index.compact.blocks[block].rom_offset;
    result.raw_crc32=m->index.compact.blocks[block].raw_crc32;
    return result;
}
static void poll_window(music_window_t *w)
{
    tr64_pack_stream_block_t info;
    if(!w->pending)return;
    info=block_info(w->block);
    if(!tr64_pack_stream_poll(&info,&w->request))return;
    w->pending=0;w->valid=1;
}
static int drain(void)
{
    uint32_t i;int done=1;
    if(!tr64_pack_stream_query_drain(&m->query))done=0;
    for(i=0;i<WINDOW_COUNT;++i) {
        poll_window(m->windows+i);
        if(m->windows[i].pending)done=0;
    }
    if(done) {
        /* No stale index record may be interpreted after the track is gone. */
        m->compacted=0;
        for(i=0;i<WINDOW_COUNT;++i) {
            m->windows[i].valid=0;m->windows[i].block=WINDOW_NONE;
        }
    }
    return done;
}
static void select_track(int32_t track)
{
    if(m->phase==PLAY && m->current==track)return;
    m->desired=track;m->phase=DRAIN;m->begin_tick=sys_profile_ticks();
}
void tr64_music_level_start(void)
{
    if(!m || !m->bound || m->active)sys_error("music start without catalog");
    m->active=1;m->have_serial=0;tr64_music_tick();
    /* Level start is still loading, not play. Prime before the first frame;
     * cue changes later use the shared per-frame allowance asynchronously. */
    while(m->phase!=STOPPED && m->phase!=PLAY) {
        tr64_io_frame();tr64_io_pump();tr64_music_pump();
    }
}
void tr64_music_level_stop(void)
{
    if(!m)return;
    m->active=0;
    /* This is a release barrier, not a gameplay bandwidth reset. Do not let
     * the query schedule another metadata page while draining its last DMA. */
    while(!drain()) { tr64_io_frame();tr64_io_pump(); }
    m->phase=STOPPED;m->bound=0;m->current=m->desired=m->ambient=-1;
    m->have_serial=0;m->block_count=0;
}
void tr64_music_tick(void)
{
    tr64_game_music_request_t request;
    if(!m || !m->active)return;
    if(m->frame_ticks>m->frame_peak_ticks)m->frame_peak_ticks=m->frame_ticks;
    m->frame_ticks=0;
    game_music_request(&request);m->ambient=request.ambient;
    if(!m->have_serial || request.serial!=m->serial) {
        int32_t track=request.reason==TR64_GAME_MUSIC_RESTORE ? request.ambient : request.track;
        m->serial=request.serial;m->have_serial=1;select_track(track);
    }
}
static int read_window(music_window_t *w,uint32_t block)
{
    int rc;tr64_pack_stream_block_t info;
    if(w->pending)return 0;
    if(w==m->windows+2 && (!m->compacted || (m->phase!=PRIME && m->phase!=PLAY)))
        sys_error("music loop window overlaps query workspace");
    w->valid=0;w->block=(uint16_t)block;memset(&w->request,0,sizeof(w->request));
    info=block_info(block);
    rc=tr64_pack_stream_read(&info,&w->request,window_data(w),WINDOW_BYTES);
    if(rc==TR64_IO_SUBMIT_BUSY)return 0;
    if(rc)sys_error("music stream submit %d",rc);
    w->pending=1;++m->reads;m->read_bytes+=info.raw_bytes;
    return 1;
}
static uint32_t position_block(uint32_t position)
{
    uint32_t offset=position/m->format.unit_frames*m->format.unit_bytes;
    uint32_t block=1+offset/m->chunk_bytes;
    if(block>=m->block_count)sys_error("music position outside stream");
    return block;
}
static music_window_t *cached(uint32_t block)
{
    uint32_t i;
    for(i=0;i<WINDOW_COUNT;++i)if(m->windows[i].valid && m->windows[i].block==block)
        return m->windows+i;
    return NULL;
}
static int scheduled(uint32_t block)
{
    uint32_t i;
    for(i=0;i<WINDOW_COUNT;++i)if((m->windows[i].valid || m->windows[i].pending) &&
                      m->windows[i].block==block)return 1;
    return 0;
}
static void prefetch(void)
{
    uint32_t wanted[3],i,j,position=m->position;
    if(position>=m->format.frames) {
        if(m->current!=m->ambient)return;
        position=0;
    }
    wanted[0]=position_block(position);wanted[1]=wanted[0]+1;wanted[2]=NONE;
    if(wanted[1]>=m->block_count)
        wanted[1]=m->current==m->ambient ? 1 : NONE;
    if(m->current==m->ambient && wanted[0]>=m->block_count-2)wanted[2]=1;
    for(i=0;i<3;++i) {
        if(wanted[i]==NONE || scheduled(wanted[i]))continue;
        for(j=0;j<WINDOW_COUNT;++j) {
            music_window_t *w=m->windows+j;
            if(!w->pending && (!w->valid ||
               (w->block!=wanted[0] && w->block!=wanted[1] && w->block!=wanted[2]))) {
                (void)read_window(w,wanted[i]);break;
            }
        }
    }
}
static void unavailable(void)
{
    ++m->unavailable;m->current=-1;
    if(m->desired!=m->ambient && m->ambient>=0)select_track(m->ambient);
    else m->phase=STOPPED;
}
static void checked_header(void)
{
    uint32_t i,offset;
    tr64_music_format(m->data[0],m->index.full[0].raw_bytes,
                       m->query.result.total_bytes,(uint32_t)m->desired,&m->format);
    if(m->format.flags&TR64_MUSIC_UNAVAILABLE) { unavailable();return; }
    m->chunk_bytes=WINDOW_BYTES/m->format.unit_bytes*m->format.unit_bytes;
    offset=m->format.data_offset;
    for(i=1;i<m->block_count;++i) {
        const tr64_pack_stream_block_t *b=m->index.full+i;
        uint32_t expected=m->format.total-offset;
        if(expected>m->chunk_bytes)expected=m->chunk_bytes;
        if(b->logical_offset!=offset || b->raw_bytes!=expected || b->raw_bytes%m->format.unit_bytes)
            sys_error("music codec physical partition");
        offset+=b->raw_bytes;
    }
    if(offset!=m->format.total || m->block_count<2)sys_error("music stream extent");
    /* Forward compaction cannot overwrite any not-yet-read full record. Only
     * after the full wire CRC AND every native partition check passed may the
     * overlaid third DMA window be used. */
    for(i=0;i<m->block_count;++i) {
        music_block_t compact={m->index.full[i].rom_offset,m->index.full[i].raw_crc32};
        m->index.compact.blocks[i]=compact;
    }
    m->compacted=1;
    tr64_music_decoder_init(&m->decoder,m->format.codec);
    m->position=m->fraction=0;m->current=m->desired;
    m->unit=m->unit_sample=m->within=0;m->play_block=1;
    m->windows[0].valid=0;m->windows[0].block=WINDOW_NONE;
    m->phase=PRIME;prefetch();
}
void tr64_music_pump(void)
{
    uint32_t i;
    if(!m || !m->active)return;
    for(i=0;i<WINDOW_COUNT;++i)poll_window(m->windows+i);
    if(m->phase==DRAIN) {
        if(!drain())return;
        m->current=-1;
        if(m->desired<0) { m->phase=STOPPED;return; }
        if(m->desired<2 || m->desired>=2+(int32_t)TRACKS ||
           !m->catalog[m->desired-2].block_count) { unavailable();return; }
        tr64_pack_stream_query_begin(&m->query,m->catalog+m->desired-2,m->pack_bytes,
            m->index.full,INDEX_CAPACITY,m->data[0],WINDOW_BYTES);
        m->phase=QUERY;
    }
    if(m->phase==QUERY) {
        if(!tr64_pack_stream_query_poll(&m->query))return;
        m->block_count=m->query.result.block_count;
        if(!m->block_count || m->index.full[0].logical_offset || m->index.full[0].raw_bytes!=64)
            sys_error("music metadata physical partition");
        m->phase=HEADER;
    }
    if(m->phase==HEADER) {
        music_window_t *w=m->windows;
        if(!w->valid && !w->pending) (void)read_window(w,0);
        if(!w->valid)return;
        checked_header();
    }
    if(m->phase==PRIME) {
        prefetch();
        if(!cached(1))return;
        m->phase=PLAY;++m->starts;
        i=sys_profile_ticks()-m->begin_tick;
        if(i>m->startup_peak_ticks)m->startup_peak_ticks=i;
    }
    if(m->phase==PLAY)prefetch();
}
static music_window_t *bind_window(uint32_t block,const uint8_t **data,uint32_t *bytes)
{
    music_window_t *w=cached(block);
    if(w) { *data=window_data(w);*bytes=block_bytes(block); }
    return w;
}
const int16_t *tr64_music_mono_pcm(uint32_t frames,uint32_t rate)
{
    music_t *s=m;music_window_t *w;uint32_t i,step,start,bytes=0;const uint8_t *data=NULL;
    const tr64_game_settings_t *settings;
    uint32_t position,fraction,unit,sample,block,within,unit_frames,unit_bytes,codec;
    if(!s || !s->active || s->phase!=PLAY)return NULL;
    settings=game_settings_view();
    if(!frames || frames>TR64_MUSIC_OUTPUT_FRAMES || rate<16000 || rate>48000)
        sys_error("music output extent/rate");
    start=sys_profile_ticks();
    /* The DAC is requested at 22050 Hz but regional divider rounding differs
     * by a few Hz. Native music plays 1:1 rather than periodically dropping a
     * sample; deliberately different output/source rates still resample. */
    step=s->format.rate==22050 && rate>=22042 && rate<=22058 ? 65536u :
         (uint32_t)(((uint64_t)s->format.rate<<16)/rate);
    position=s->position;fraction=s->fraction;unit=s->unit;sample=s->unit_sample;
    block=s->play_block;within=s->within;
    unit_frames=s->format.unit_frames;unit_bytes=s->format.unit_bytes;codec=s->format.codec;
    w=bind_window(block,&data,&bytes);
    const uint32_t length=s->format.frames;
    for(i=0;i<frames;) {
        if(position>=length) {
            if(s->current==s->ambient) {
                position-=length;++s->loops;
                tr64_music_decoder_init(&s->decoder,codec);
                unit=within=0;sample=position;block=1;w=bind_window(1,&data,&bytes);
            } else {
                memset(s->output+i,0,(frames-i)*sizeof(*s->output));
                select_track(s->ambient);break;
            }
        }
        if(!w) { ++s->underruns;memset(s->output+i,0,(frames-i)*sizeof(*s->output));break; }
        /* Drain one decoded unit at a time. Interleaving a stack spill with
         * decoder.unit on every sample thrashed one VR4300 cache line when
         * the play stack grew; the copy/resample loop needs neither. */
        if(s->decoder.unit!=unit)
            (void)tr64_music_decoder_sample(&s->decoder,data+within,unit,sample);
        /* If a one-frame loop remains at EOF after an overshooting step,
         * consume exactly one output before wrapping again. */
        uint32_t n=1;
        if(position<length) {
            uint32_t span=unit_frames-sample;
            const uint32_t left=length-position;
            if(span>left)span=left;
            n=step==65536u ? span : ((span<<16)-fraction+step-1)/step;
            if(n>frames-i)n=frames-i;
        }
        if(step==65536u) {
            memcpy(s->output+i,s->decoder.decoded+sample,n*sizeof(*s->output));
            position+=n;sample+=n;
        } else {
            uint32_t phase=(sample<<16)|fraction;
            int16_t *dst=s->output+i;
            const int16_t *src=s->decoder.decoded;
            for(uint32_t j=0;j<n;++j) { dst[j]=src[phase>>16];phase+=step; }
            position+=(phase>>16)-sample;sample=phase>>16;fraction=phase&65535u;
        }
        i+=n;
        if(sample>=unit_frames) {
            sample-=unit_frames;++unit;within+=unit_bytes;
            if(within==bytes) { within=0;++block;w=bind_window(block,&data,&bytes); }
        }
    }
    s->position=position;s->fraction=fraction;s->unit=unit;s->unit_sample=sample;
    s->play_block=block;s->within=within;
    s->frames+=frames;start=sys_profile_ticks()-start;s->frame_ticks+=start;
    s->mix_us+=sys_profile_us(start);if(start>s->peak_ticks)s->peak_ticks=start;
    return settings && settings->audio_music ? s->output : NULL;
}
/* Running total of the decode and resampling time in tr64_music_mono_pcm, microseconds modulo 2^32. */
uint32_t tr64_music_perf_us(void) { return m ? (uint32_t)m->mix_us : 0u; }
void tr64_music_report(void)
{
    if(!m)return;
    log_printf("music: starts=%lu loops=%lu unavailable=%lu underruns=%lu frames=%lu reads=%lu bytes=%lu arena=%lu mix_us=%llu peak_buffer_us=%lu frame_peak_us=%lu startup_peak_us=%lu codec=%lu rate=%lu current=%ld phase=%lu\n",
        (unsigned long)m->starts,(unsigned long)m->loops,(unsigned long)m->unavailable,
        (unsigned long)m->underruns,(unsigned long)m->frames,(unsigned long)m->reads,
        (unsigned long)m->read_bytes,(unsigned long)sizeof(*m),(unsigned long long)m->mix_us,
        (unsigned long)sys_profile_us(m->peak_ticks),
        (unsigned long)sys_profile_us(m->frame_ticks>m->frame_peak_ticks?m->frame_ticks:m->frame_peak_ticks),
        (unsigned long)sys_profile_us(m->startup_peak_ticks),
        (unsigned long)m->format.codec,(unsigned long)m->format.rate,(long)m->current,(unsigned long)m->phase);
}
