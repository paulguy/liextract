#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "riff.h"

#define SHORT_FROM_ARRAY(ARRAY, INDEX) (*(short int *)(&((ARRAY)[(INDEX)])))
#define INT_FROM_ARRAY(ARRAY, INDEX) (*(int *)(&((ARRAY)[(INDEX)])))

#define OMNI_TRACK_TYPE_AUDIO (4)
#define OMNI_TRACK_TYPE_VIDEO (7)
#define OMNI_TRACK_TYPE_FLIC  (3)

#define OMNI_CHUNK_HEADER_SIZE (14)
#define OMNI_CHUNK_VIDEO_HEADER_SIZE (20)

#define OMNI_CHUNK_TYPE_DATA (0)
#define OMNI_CHUNK_TYPE_PARTIAL (16)
#define OMNI_CHUNK_TYPE_LAST (2)

const char WAVType[] = {'W', 'A', 'V', 'E'};
const char fmtHdr[] = {'f', 'm', 't', ' '};
const char dataHdr[] = {'d', 'a', 't', 'a'};
typedef struct {
    char RIFF[4];
    int fileSize;
    char WAVE[4];
    char fmt[4];
    int fmtSize;
    short int format;
    short int channels;
    int sampleRate;
    int bytesPerSecond;
    short int bytesPerSample;
    short int bitsPerSample;
    char data[4];
    int dataSize;
} WAVHeader;

#define WAV_FILE_SIZE_OFFSET (4)
#define WAV_DATA_SIZE_OFFSET (40)
#define WAV_FILE_SIZE_ADD    (sizeof(WAVHeader) - 8)

typedef struct __attribute__((packed)) {
    int dataSize;
    short int type;
    short int frames;
    short int width;
    short int height;
    short int depth;
    short int flags;
    int speed;
    short int reserved0;
    int created;
    int creator;
    int updated;
    int updator;
    short int aspectX;
    short int aspectY;
    char extended[14];
    char reserved1[24];
    int firstFrame;
    int secondFrame;
    char reserved2[40];
} FLCHeader;

typedef struct {
    unsigned int size;
    unsigned char data[22064];

    /* required fields */
    short int chunkType;
    int trackNum;
    int timestamp;
    int hdrSize;
} Chunk;

typedef struct {
    char *trackName;
    int trackType;
    int trackNum;
    int unk0;
    int unk1;
    int unk2;
    int unk3;
    int unk4;

    /* for video types, these values are not in the main MxOb */
    char *fileName;
    char format[4];
    int unk5;

    /* additional fields for FLIC type */
    short int unk6;
    int unk7;
    int unk8;
    int unk9;
} MxOb;

typedef struct {
    unsigned char MxOb[1024];
    int index;

    MxOb mainMxOb;
    MxOb videoMxOb;
    MxOb audioMxOb;

    /* fields from first audio chunk */
    /* WAV fmt header */
    WAVHeader wav;

    /* fields from first video chunk */
    /* FLIC header */
    FLCHeader flc;

    Chunk *c;
    unsigned int chunks;
} Track;

int print_entry_cb(RIFFFile *r, int dir, int ent, void *priv) {
    int depth = 1;
    off_t filePos = 0;
    int ent2;
    int index = RIFF_ENTRY(r, dir, ent);
    const char *fourCC;
    char type;

    for(ent2 = index; ent2 != 0; ent2 = r->root[ent2].parent) {
        filePos += r->root[ent2].start;
        depth++;
    }

    fourCC = r->root[index].fourCC;
    if(isNode(r->root[index].fourCC)) {
        if(isLIST(r->root[index].fourCC)) {
            fourCC = r->root[index].fourCC2;
        }
        type = 'd';
    } else {
        type = 'f';
    }

    printf("%*c %3d %9lu %9lu %8d %c%c%c%c\n", depth, type, ent,
           r->root[index].start, filePos, r->root[index].size,
           fourCC[0], fourCC[1], fourCC[2], fourCC[3]);

    return(0);
}

void populate_mxob(MxOb *o, unsigned char *buf, Track *t) {
    int dataPos = 0;
    int len;

    o->trackType = INT_FROM_ARRAY(buf, 0);

    o->trackName = (char *)&(buf[7]);
    dataPos = 7 + strlen(o->trackName) + 1;

    o->trackNum = INT_FROM_ARRAY(buf, dataPos);
    o->unk0 = INT_FROM_ARRAY(buf, dataPos + 4);
    o->unk1 = INT_FROM_ARRAY(buf, dataPos + 12);
    o->unk2 = INT_FROM_ARRAY(buf, dataPos + 16);
    if(o->trackType == OMNI_TRACK_TYPE_FLIC) {
        o->unk6 = SHORT_FROM_ARRAY(buf, dataPos + 24);
        o->unk7 = INT_FROM_ARRAY(buf, dataPos + 26);
        o->unk8 = INT_FROM_ARRAY(buf, dataPos + 34);
        o->unk9 = INT_FROM_ARRAY(buf, dataPos + 42);
    }
    o->unk3 = INT_FROM_ARRAY(buf, dataPos + 66);
    o->unk4 = INT_FROM_ARRAY(buf, dataPos + 82);
    if(o->trackType != OMNI_TRACK_TYPE_VIDEO) {
        o->fileName = (char *)&(buf[dataPos + 94]);
        dataPos += 94 + strlen(o->fileName) + 1;
        memcpy(o->format, &(buf[dataPos + 12]), sizeof(o->format));
        if(t != NULL) { /* not available if it's a member of a video track */
            o->unk5 = INT_FROM_ARRAY(buf, dataPos + 24);
        }
    } else {
        if(t == NULL) {
            fprintf(stderr, "Track is null?\n");
            return;
        }
        dataPos += 94 + 12; /* skip over MxOb chunk and LIST header*/
        o->unk5 = INT_FROM_ARRAY(buf, dataPos);
        len = INT_FROM_ARRAY(buf, dataPos + 8); /* read length of MxCh chunk */
        len += (len % 2) ? 1 : 0; /* advertises 185 but should be 186, assume these also need 2 byte alignment for length like other chunks */
        dataPos += 12; /* skip over value and MxOb header */
        populate_mxob(&(t->videoMxOb), &(buf[dataPos]), NULL);
        dataPos += len + 8; /* skip over MxOb chunk and next header */
        populate_mxob(&(t->audioMxOb), &(buf[dataPos]), NULL);
    }
}

int get_track_info_cb(RIFFFile *r, int dir, int ent, void *priv) {
    int index = RIFF_ENTRY(r, dir, ent);
    Track *t = priv;

    if(r->root[index].size > sizeof(t->MxOb)) {
        fprintf(stderr, "MxOb too big.\n");
        return(-1);
    }

    if(riff_entry_seekto(r, index) < 0) {
        fprintf(stderr, "Failed to seek to MxOb.\n");
        return(-1);
    }

    if(fread(t->MxOb, 1, r->root[index].size, r->f) < r->root[index].size) {
        fprintf(stderr, "Failed to read MxOb.\n");
        return(-1);
    }

    populate_mxob(&(t->mainMxOb), t->MxOb, t);

    return(1); /* stop searching */
}

Chunk *track_grow(Track *t) {
    Chunk *c2;

    c2 = realloc(t->c, sizeof(Chunk) * (t->chunks + 1));
    if(c2 == NULL) {
        fprintf(stderr, "Failed to allocate memory to grow entry table.\n");
        return(NULL);
    }

    t->c = c2;
    t->chunks++;

    return(&(t->c[t->chunks-1]));
}

int read_chunks_cb(RIFFFile *r, int dir, int ent, void *priv) {
    Track *t = priv;
    Chunk *c;
    int index = RIFF_ENTRY(r, dir, ent);
    unsigned char *body;

    if(r->root[index].size > sizeof(c->data)) {
        fprintf(stderr, "Chunk data too large.\n");
        goto error;
    }

    if(riff_entry_seekto(r, index) < 0) {
        fprintf(stderr, "Failed to seek to MxCh.\n");
        goto error;
    }

    c = track_grow(t);
    if(c == NULL) {
        goto error;
    }

    c->size = r->root[index].size;
    if(fread(c->data, 1, c->size, r->f) < c->size) {
        fprintf(stderr, "Failed to read MxCh.\n");
        goto error;
    }

    c->chunkType = SHORT_FROM_ARRAY(c->data, 0);
    c->trackNum = INT_FROM_ARRAY(c->data, 2);
    c->timestamp = INT_FROM_ARRAY(c->data, 6);
    c->hdrSize = INT_FROM_ARRAY(c->data, 10);

    /* this pointer may become invalid later, but it will be assigned again */
    body = &(c->data[OMNI_CHUNK_HEADER_SIZE]);
    if(
          (
              (
                  t->mainMxOb.trackType == OMNI_TRACK_TYPE_VIDEO &&
                  c->trackNum == t->audioMxOb.trackNum
              ) || (
                  t->mainMxOb.trackType == OMNI_TRACK_TYPE_AUDIO
              )
          ) && t->wav.dataSize == 0
      ) {
        t->wav.format = SHORT_FROM_ARRAY(body, 0);
        t->wav.channels = SHORT_FROM_ARRAY(body, 2);
        t->wav.sampleRate = INT_FROM_ARRAY(body, 4);
        t->wav.bytesPerSecond = INT_FROM_ARRAY(body, 8);
        t->wav.bytesPerSample = SHORT_FROM_ARRAY(body, 12);
        t->wav.bitsPerSample = SHORT_FROM_ARRAY(body, 14);
        t->wav.dataSize = -1;
        t->wav.fileSize = -1;
    } else if(t->mainMxOb.trackType == OMNI_TRACK_TYPE_VIDEO &&
              c->trackNum == t->videoMxOb.trackNum) {
        if(t->flc.dataSize == 0) {
            t->flc.dataSize = INT_FROM_ARRAY(body, 0);
            t->flc.type = SHORT_FROM_ARRAY(body, 4);
            t->flc.frames = SHORT_FROM_ARRAY(body, 6);
            t->flc.width = SHORT_FROM_ARRAY(body, 8);
            t->flc.height = SHORT_FROM_ARRAY(body, 10);
            t->flc.depth = SHORT_FROM_ARRAY(body, 12);
            t->flc.flags = SHORT_FROM_ARRAY(body, 14);
            t->flc.speed = INT_FROM_ARRAY(body, 16);
            t->flc.created = INT_FROM_ARRAY(body, 22);
            t->flc.creator = INT_FROM_ARRAY(body, 26);
            t->flc.updated = INT_FROM_ARRAY(body, 30);
            t->flc.updator = INT_FROM_ARRAY(body, 34);
            t->flc.aspectX = SHORT_FROM_ARRAY(body, 38);
            t->flc.aspectY = SHORT_FROM_ARRAY(body, 40);
            t->flc.firstFrame = INT_FROM_ARRAY(body, 80);
            t->flc.secondFrame = INT_FROM_ARRAY(body, 84);
        }
    }

    return(0);

error:
    if(t->c != NULL) free(t->c);
    return(-1);
}

void print_mxob(MxOb *o) {
    fprintf(stderr, "Name: %s\n"
                    "Type: %d\n"
                    "Track num: %d\n"
                    "Unknown 0: %d\n"
                    "Unknown 1: %d\n"
                    "Unknown 2: %d\n"
                    "Unknown 3: %08X\n"
                    "Unknown 4: %08X\n",
                    o->trackName,
                    o->trackType,
                    o->trackNum,
                    o->unk0,
                    o->unk1,
                    o->unk2,
                    o->unk3,
                    o->unk4);
}

int dump_song_cb(RIFFFile *r, int dir, int ent, void *priv) {
    Track *t = priv;
    t->index = RIFF_ENTRY(r, dir, ent);
    unsigned int i;
    FILE *outWAV = NULL;
    FILE *outFLC = NULL;
    unsigned char *body;
    unsigned int toWrite, audioWritten, videoWritten;

    t->mainMxOb.trackType = 0;

    if(do_traverse(r, "MxOb", get_track_info_cb, t, 0, t->index) < 0) {
        goto error0;
    }

    if(t->mainMxOb.trackType == 0) {
        fprintf(stderr, "Failed to find MxOb.\n");
        goto error0;
    }

    fprintf(stderr, "Index: %d\n"
                    "Main MxOb\n",
                    t->index);
    print_mxob(&(t->mainMxOb));

    if(t->mainMxOb.trackType != OMNI_TRACK_TYPE_VIDEO) {
        fprintf(stderr, "File name: %s\n"
                        "Unknown 5: %d\n",
                        t->mainMxOb.fileName,
                        t->mainMxOb.unk5);
    } else {
        fprintf(stderr, "Unknown 5: %d\n",
                        t->mainMxOb.unk5);
        fprintf(stderr, "FLIC MxOb\n");
        print_mxob(&(t->videoMxOb));
        fprintf(stderr, "File name: %s\n"
                        "Unknown 5: %d\n"
                        "Unknown 6: %hd\n"
                        "Unknown 7: %d\n"
                        "Unknown 8: %d\n"
                        "Unknown 9: %d\n",
                        t->videoMxOb.fileName,
                        t->videoMxOb.unk5,
                        t->videoMxOb.unk6,
                        t->videoMxOb.unk7,
                        t->videoMxOb.unk8,
                        t->videoMxOb.unk9);
        fprintf(stderr, "WAVE MxOb\n");
        print_mxob(&(t->audioMxOb));
        fprintf(stderr, "File name: %s\n"
                        "Unknown 5: %d\n",
                        t->audioMxOb.fileName,
                        t->audioMxOb.unk5);
   }

    /* allow the callback to detect whether the audio format fields have been
    populated */
    t->wav.dataSize = 0;
    t->flc.dataSize = 0;
    t->chunks = 0;
    t->c = NULL;

    if(do_traverse(r, "MxDaMxCh", read_chunks_cb, t, 0, t->index) < 0) {
        goto error0;
    }

    fprintf(stderr, "Read %d chunks.\n", t->chunks);

    audioWritten = 0;
    videoWritten = 0;
    for(i = 0; i < t->chunks; i++) {
        if(
              (
                  t->mainMxOb.trackType == OMNI_TRACK_TYPE_VIDEO &&
                  t->c[i].trackNum == t->audioMxOb.trackNum
              ) || (
                  t->mainMxOb.trackType == OMNI_TRACK_TYPE_AUDIO
              )
          ) {
            if(outWAV == NULL) {
                if(t->mainMxOb.trackType == OMNI_TRACK_TYPE_AUDIO) {
                    outWAV = fopen(t->mainMxOb.trackName, "wb");
                } else {
                    outWAV = fopen(t->audioMxOb.trackName, "wb");
                }
                if(outWAV == NULL) {
                    fprintf(stderr, "Failed to open file %s for writing.\n", t->mainMxOb.trackName);
                    goto error1;
                }

                if(fwrite(&(t->wav), 1, sizeof(t->wav), outWAV) < sizeof(t->wav)) {
                    fprintf(stderr, "Failed to write WAV header.\n");
                    goto error1;
                }
            } else if(t->c[i].chunkType != OMNI_CHUNK_TYPE_LAST) { /* don't write first or last chunks */
                body = &(t->c[i].data[OMNI_CHUNK_HEADER_SIZE]);
                toWrite = t->c[i].size - OMNI_CHUNK_HEADER_SIZE;
                if(fwrite(body, 1, toWrite, outWAV) < toWrite) {
                    fprintf(stderr, "Failed to write audio data: %s\n", strerror(errno));
                    goto error1;
                }
                audioWritten += toWrite;
            }
        } else if(t->mainMxOb.trackType == OMNI_TRACK_TYPE_VIDEO &&
                  t->c[i].trackNum == t->videoMxOb.trackNum) {
            if(outFLC == NULL) {
                outFLC = fopen(t->videoMxOb.trackName, "wb");        
                if(outFLC == NULL) {
                    fprintf(stderr, "Failed to open file %s for writing.\n", t->mainMxOb.trackName);
                    goto error1;
                }

                if(fwrite(&(t->flc), 1, sizeof(t->flc), outFLC) < sizeof(t->flc)) {
                    fprintf(stderr, "Failed to write WAV header.\n");
                    goto error1;
                }
            } else if(t->c[i].chunkType != OMNI_CHUNK_TYPE_LAST) { /* same */
                body = &(t->c[i].data[OMNI_CHUNK_HEADER_SIZE + OMNI_CHUNK_VIDEO_HEADER_SIZE]);
                toWrite = t->c[i].size - OMNI_CHUNK_HEADER_SIZE - OMNI_CHUNK_VIDEO_HEADER_SIZE;
                if(fwrite(body, 1, toWrite, outFLC) < toWrite) {
                    fprintf(stderr, "Failed to write audio data: %s\n", strerror(errno));
                    goto error1;
                }
                videoWritten += toWrite;
            }
        }
    }

    fprintf(stderr, "%d %d %d %d\n", audioWritten, t->wav.dataSize,
                                     videoWritten + 128, t->flc.dataSize);

    if(fseeko(outWAV, WAV_DATA_SIZE_OFFSET, SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to WAV data size offset.\n");
        goto error1;
    }
    if(fwrite(&audioWritten, 1, sizeof(int), outWAV) < sizeof(int)) {
        fprintf(stderr, "Failed to write WAV data size.\n");
        goto error1;
    }
    audioWritten += WAV_FILE_SIZE_ADD;
    if(fseeko(outWAV, WAV_FILE_SIZE_OFFSET, SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to WAV file size offset.\n");
        goto error1;
    }
    if(fwrite(&audioWritten, 1, sizeof(int), outWAV) < sizeof(int)) {
        fprintf(stderr, "Failed to write WAV file size.\n");
        goto error1;
    }

    if(outFLC) fclose(outFLC);
    if(outWAV) fclose(outWAV);
    free(t->c);

    fprintf(stderr, "Wrote to %s.\n\n", t->mainMxOb.trackName);

    return(0);

error1:
    if(outFLC) fclose(outFLC);
    if(outWAV) fclose(outWAV);
    free(t->c);
error0:
    return(-1);
}

void usage(const char *argv0) {
    fprintf(stderr, "USAGE: %s <list|extract> <filename>\n", argv0);
}

int main(int argc, char **argv) {
    RIFFFile *r;
    int extract;
    Track t;

    if(argc < 3) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if(strcmp(argv[1], "list") == 0) {
        extract = 0;
    } else if(strcmp(argv[1], "extract") == 0) {
        extract = 1;
    } else {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    r = riff_open(argv[2]);
    if(r == NULL) {
        fprintf(stderr, "Failed to open.\n");
        exit(EXIT_FAILURE);
    }

    if(extract == 0) {
        if(riff_traverse(r, "", print_entry_cb, NULL) < 0) {
            fprintf(stderr, "Failed to traverse file.\n");
        }
    } else {
        /* set up initial fields in track */
        memcpy(t.wav.RIFF, RIFFMagic, sizeof(t.wav.RIFF));
        memcpy(t.wav.WAVE, WAVType, sizeof(t.wav.WAVE));
        memcpy(t.wav.fmt, fmtHdr, sizeof(t.wav.fmt));
        t.wav.fmtSize = 16;
        memcpy(t.wav.data, dataHdr, sizeof(t.wav.data));
        t.flc.reserved0 = 0;
        memset(t.flc.extended, 0, sizeof(t.flc.extended));
        memset(t.flc.reserved1, 0, sizeof(t.flc.reserved1));
        memset(t.flc.reserved2, 0, sizeof(t.flc.reserved2));
        if(riff_traverse(r, "MxStMxSt", dump_song_cb, &t) < 0) {
            fprintf(stderr, "Failed to traverse file.\n");
        }
    }

    riff_close(r);

    exit(EXIT_SUCCESS);
}
