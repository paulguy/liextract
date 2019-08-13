#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "riff.h"

#define SHORT_FROM_ARRAY(ARRAY, INDEX) (*(short int *)(&((ARRAY)[(INDEX)])))
#define INT_FROM_ARRAY(ARRAY, INDEX) (*(int *)(&((ARRAY)[(INDEX)])))

#define OMNI_TRACK_TYPE_WAVE    (4)
#define OMNI_TRACK_TYPE_MUXED   (7)
#define OMNI_TRACK_TYPE_RAW     (3)
#define OMNI_TRACK_TYPE_BITMAP  (10)

#define OMNI_CHUNK_HEADER_SIZE (14)
#define OMNI_CHUNK_FLC_HEADER_SIZE (20)

#define OMNI_CHUNK_TYPE_DATA (0)
#define OMNI_CHUNK_TYPE_PARTIAL (16)
#define OMNI_CHUNK_TYPE_LAST (2)

const char WAVType[] = {'W', 'A', 'V', 'E'};
const char fmtHdr[] = {'f', 'm', 't', ' '};
const char dataHdr[] = {'d', 'a', 't', 'a'};

typedef struct __attribute__((packed)) {
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
    char b, g, r;
} PalEntry;

typedef struct __attribute__((packed)) {
    char IDLength;
    char colorMapType;
    char dataTypeCode;
    short int colorMapStart;
    short int colorMapLength;
    char colorMapDepth;
    short int originX;
    short int originY;
    short int width;
    short int height;
    char bitsPerPixel;
    char descriptor;

    PalEntry pal[256];
} TGAHeader;

typedef struct {
    unsigned int size;
    unsigned char data[65536];

    /* required fields */
    short int chunkType;
    int trackNum;
    int timestamp;
    int hdrSize;
} Chunk;

const char FLCfmt[] = {' ', 'F', 'L', 'C'};

typedef struct {
    FILE *out;

    /* for WAV tracks */
    WAVHeader wav;

    /* for STL bitmap objects */
    TGAHeader tga;

    char *trackName;
    short int trackType;
    unsigned int trackNum;
    int unk0;
    int unk1;
    int unk2;
    int unk3;
    int unk4;
    short int unkNameLength;

    /* for video types, these values are not in the main MxOb */
    char *fileName;
    char format[4];
    int unk5;

    /* for muxed type, the number of objects following */
    unsigned int objects;

    /* additional fields for raw type */
    short int unk6;
    int unk7;
    int unk8;
    int unk9;
} MxOb;

typedef struct {
    int index;

    unsigned char MxObData[65536];
    unsigned int MxObLength;
    MxOb *mxob;
    unsigned int mxobs;

    /* fields from first audio chunk */
    /* WAV fmt header */

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

MxOb *mxob_grow(Track *t) {
    MxOb *m2;

    m2 = realloc(t->mxob, sizeof(MxOb) * (t->mxobs + 1));
    if(m2 == NULL) {
        fprintf(stderr, "Failed to allocate memory to grow MxOb table.\n");
        return(NULL);
    }

    t->mxob = m2;
    t->mxobs++;

    return(&(t->mxob[t->mxobs-1]));
}

int populate_mxob(Track *t, unsigned char *buf, unsigned int length) {
    int len;
    MxOb *o;
    unsigned int dataPos = 0;

    o = mxob_grow(t);
    if(o == NULL) {
        return(-1);
    }
    o->out = NULL;

    o->trackType = SHORT_FROM_ARRAY(buf, dataPos);
     /* flag as uninitialized */
    if(o->trackType == OMNI_TRACK_TYPE_WAVE) {
        o->wav.fileSize = 0;
    } else if(o->trackType == OMNI_TRACK_TYPE_BITMAP) {
        o->tga.dataTypeCode = 0;
    }

    /* If there's a string directly after the value, discard it. */
    for(dataPos = 2; dataPos < length; dataPos++) {
        if(buf[dataPos] == 0) {
            break;
        }
    }
    /* find the start of the name */
    for(; dataPos < length; dataPos++) {
        if(buf[dataPos] != 0) {
            break;
        }
    }

    o->trackName = (char *)&(buf[dataPos]);
    dataPos += strlen(o->trackName) + 1;

    o->trackNum = INT_FROM_ARRAY(buf, dataPos);
    o->unk0 = INT_FROM_ARRAY(buf, dataPos + 4);
    o->unk1 = INT_FROM_ARRAY(buf, dataPos + 12);
    o->unk2 = INT_FROM_ARRAY(buf, dataPos + 16);
    if(o->trackType == OMNI_TRACK_TYPE_RAW) {
        o->unk6 = SHORT_FROM_ARRAY(buf, dataPos + 24);
        o->unk7 = INT_FROM_ARRAY(buf, dataPos + 26);
        o->unk8 = INT_FROM_ARRAY(buf, dataPos + 34);
        o->unk9 = INT_FROM_ARRAY(buf, dataPos + 42);
    }
    o->unk3 = INT_FROM_ARRAY(buf, dataPos + 66);
    o->unk4 = INT_FROM_ARRAY(buf, dataPos + 82);
    o->unkNameLength = SHORT_FROM_ARRAY(buf, dataPos + 92);
    if(o->trackType != OMNI_TRACK_TYPE_MUXED) {
        o->fileName = (char *)&(buf[dataPos + 94]);
        dataPos += 94 + strlen(o->fileName) + 1;
        memcpy(o->format, &(buf[dataPos + 12]), sizeof(o->format));
        if(t != NULL) { /* not available if it's a member of a video track */
            o->unk5 = INT_FROM_ARRAY(buf, dataPos + 24);
        }
    } else {
        dataPos += o->unkNameLength;
        dataPos += 94 + 12; /* skip over MxOb chunk and LIST header */
        o->objects = INT_FROM_ARRAY(buf, dataPos);
        dataPos += 4; /* skip over value */
        while(dataPos < length - 1) {
            len = INT_FROM_ARRAY(buf, dataPos + 4); /* read length of MxCh chunk */
            len += (len % 2) ? 1 : 0; /* seems to need 2 byte alignment for length like other chunks */
            dataPos += 8; /* skip over MxOb header */
            if(populate_mxob(t, &(buf[dataPos]), len) < 0) {
                return(-1);
            }
            dataPos += len; /* skip over MxOb chunk */
        }
    }

    return(0);
}

int get_track_info_cb(RIFFFile *r, int dir, int ent, void *priv) {
    int index = RIFF_ENTRY(r, dir, ent);
    Track *t = priv;

    if(r->root[index].size > sizeof(t->MxObData)) {
        fprintf(stderr, "MxOb too big.\n");
        return(-1);
    }

    if(riff_entry_seekto(r, index) < 0) {
        fprintf(stderr, "Failed to seek to MxOb.\n");
        return(-1);
    }

    t->MxObLength = r->root[index].size;
    if(fread(t->MxObData, 1, t->MxObLength, r->f) < t->MxObLength) {
        fprintf(stderr, "Failed to read MxOb.\n");
        return(-1);
    }

    if(populate_mxob(t, t->MxObData, t->MxObLength) < 0) {
        return(-1);
    }

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

MxOb *get_trackNum(Track *t, unsigned int trackNum) {
    unsigned int i;

    for(i = 0; i < t->mxobs; i++) {
        if(t->mxob[i].trackNum == trackNum) {
            return(&(t->mxob[i]));
        }
    }

    return(NULL);
}

int read_chunks_cb(RIFFFile *r, int dir, int ent, void *priv) {
    Track *t = priv;
    Chunk *c;
    MxOb *o;
    int index = RIFF_ENTRY(r, dir, ent);
    unsigned char *body;
    unsigned int i;

    if(r->root[index].size > sizeof(c->data)) {
        fprintf(stderr, "Chunk data too large.\n");
        return(-1);
    }

    if(riff_entry_seekto(r, index) < 0) {
        fprintf(stderr, "Failed to seek to MxCh.\n");
        return(-1);
    }

    c = track_grow(t);
    if(c == NULL) {
        return(-1);
    }

    c->size = r->root[index].size;
    if(fread(c->data, 1, c->size, r->f) < c->size) {
        fprintf(stderr, "Failed to read MxCh.\n");
        return(-1);
    }

    c->chunkType = SHORT_FROM_ARRAY(c->data, 0);
    c->trackNum = INT_FROM_ARRAY(c->data, 2);
    c->timestamp = INT_FROM_ARRAY(c->data, 6);
    c->hdrSize = INT_FROM_ARRAY(c->data, 10);

    o = get_trackNum(t, c->trackNum);
    if(o == NULL) {
        fprintf(stderr, "Couldn't find object associated with track %u.\n", c->trackNum);
        return(-1);
    }
     /* this pointer may become invalid later, but it will be assigned again */
    body = &(c->data[OMNI_CHUNK_HEADER_SIZE]);
    if(o->trackType == OMNI_TRACK_TYPE_WAVE && o->wav.fileSize == 0) {
        /* set up initial fields in track */
        memcpy(o->wav.RIFF, RIFFMagic, sizeof(o->wav.RIFF));
        memcpy(o->wav.WAVE, WAVType, sizeof(o->wav.WAVE));
        memcpy(o->wav.fmt, fmtHdr, sizeof(o->wav.fmt));
        o->wav.fmtSize = 16;
        memcpy(o->wav.data, dataHdr, sizeof(o->wav.data));
        o->wav.dataSize = -1;
        o->wav.fileSize = -1;

        /* fill out what we know */
        o->wav.format = SHORT_FROM_ARRAY(body, 0);
        o->wav.channels = SHORT_FROM_ARRAY(body, 2);
        o->wav.sampleRate = INT_FROM_ARRAY(body, 4);
        o->wav.bytesPerSecond = INT_FROM_ARRAY(body, 8);
        o->wav.bytesPerSample = SHORT_FROM_ARRAY(body, 12);
        o->wav.bitsPerSample = SHORT_FROM_ARRAY(body, 14);
    } else if(o->trackType == OMNI_TRACK_TYPE_BITMAP && o->tga.dataTypeCode == 0) {
        /* convert the existing header to Targa. */
        o->tga.IDLength = 0;
        o->tga.colorMapType = 1;
        o->tga.dataTypeCode = 1; /* indexed */
        o->tga.colorMapStart = 0;
        o->tga.colorMapLength = 256;
        o->tga.colorMapDepth = 24;
        o->tga.originX = 0;
        o->tga.originY = 0;
        o->tga.width = INT_FROM_ARRAY(body, 4);
        o->tga.height = INT_FROM_ARRAY(body, 8);
        o->tga.width = (o->tga.width / 4 + ((o->tga.width % 4) ? 1 : 0)) * 4;
        o->tga.bitsPerPixel = 8;
        o->tga.descriptor = 0;

        for(i = 0; i < 256; i++) {
            o->tga.pal[i].r = body[40 + (i * 4)];
            o->tga.pal[i].g = body[40 + (i * 4) + 1];
            o->tga.pal[i].b = body[40 + (i * 4) + 2];
        }
    }

    return(0);
}

void print_mxob(MxOb *o) {
    printf("Name: %s\n"
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
    unsigned char *body;
    unsigned int toWrite;
    MxOb *o;

    t->mxobs = 0;
    t->mxob = NULL;

    if(do_traverse(r, "MxOb", get_track_info_cb, t, 0, t->index) < 0) {
        goto error0;
    }

    if(t->mxob == NULL) {
        fprintf(stderr, "Failed to find MxOb.\n");
        goto error0;
    }

    printf("Index: %d\n"
           "Main MxOb\n",
           t->index);
    print_mxob(&(t->mxob[0]));

    if(t->mxob[0].trackType != OMNI_TRACK_TYPE_MUXED) {
        printf("File name: %s\n"
               "Unknown 5: %d\n",
               t->mxob[0].fileName,
               t->mxob[0].unk5);
    } else {
        printf("Sub Objects: %d\n",
               t->mxob[0].objects);
        for(i = 1; i < t->mxobs; i++) {
            if(t->mxob[i].trackType == OMNI_TRACK_TYPE_WAVE) {
                printf("WAVE MxOb\n"
                       "Index: %d\n",
                       i);
                print_mxob(&(t->mxob[i]));
                printf("File name: %s\n"
                       "Unknown 5: %d\n",
                       t->mxob[i].fileName,
                       t->mxob[i].unk5);
            } else {
                printf("raw file data MxOb\n"
                       "Index: %d\n",
                       i);
                print_mxob(&(t->mxob[i]));
                printf("File name: %s\n"
                       "Unknown 5: %d\n"
                       "Unknown 6: %hd\n"
                       "Unknown 7: %d\n"
                       "Unknown 8: %d\n"
                       "Unknown 9: %d\n",
                       t->mxob[i].fileName,
                       t->mxob[i].unk5,
                       t->mxob[i].unk6,
                       t->mxob[i].unk7,
                       t->mxob[i].unk8,
                       t->mxob[i].unk9);
            }
        }
    }

    t->chunks = 0;
    t->c = NULL;

    if(do_traverse(r, "MxDaMxCh", read_chunks_cb, t, 0, t->index) < 0) {
        goto error1;
    }

    printf("Read %d chunks.\n", t->chunks);

    for(i = 0; i < t->chunks; i++) {
        /* don't care about empty chunks */
        if(t->c[i].chunkType == OMNI_CHUNK_TYPE_LAST) {
            continue;
        }

        o = get_trackNum(t, t->c[i].trackNum);
        if(o == NULL) {
            fprintf(stderr, "Couldn't find object associated with track %u.\n",
                            t->c[i].trackNum);
            goto error2;
        }
        /* not concerned about the container */
        if(o->trackType == OMNI_TRACK_TYPE_MUXED) {
            continue;
        }

        if(o->out == NULL) {
            o->out = fopen(o->trackName, "wb");
            if(o->out == NULL) {
                fprintf(stderr, "Failed to open file %s for writing.\n", o->trackName);
                goto error2;
            }
            printf("Opened %s.\n", o->trackName);

            if(o->trackType == OMNI_TRACK_TYPE_WAVE) {
                if(fwrite(&(o->wav), 1, sizeof(o->wav), o->out) < sizeof(o->wav)) {
                    fprintf(stderr, "Failed to write WAV header.\n");
                    goto error2;
                }
            } else if(o->trackType == OMNI_TRACK_TYPE_BITMAP) {
                if(fwrite(&(o->tga), 1, sizeof(o->tga), o->out) < sizeof(o->tga)) {
                    fprintf(stderr, "Failed to write TGA header: %s\n", strerror(errno));
                    goto error2;
                }
            } else { /* first chunk is always fully written */
                body = &(t->c[i].data[OMNI_CHUNK_HEADER_SIZE]);
                toWrite = t->c[i].size - OMNI_CHUNK_HEADER_SIZE;
                if(fwrite(body, 1, toWrite, o->out) < toWrite) {
                    fprintf(stderr, "Failed to write data: %s\n", strerror(errno));
                    goto error2;
                }
            }
        } else {
            if(o->trackType == OMNI_TRACK_TYPE_RAW &&
               !memcmp(o->format, FLCfmt, sizeof(FLCfmt))) {
                /* further FLC chunks have some extra data */
                body = &(t->c[i].data[OMNI_CHUNK_FLC_HEADER_SIZE + OMNI_CHUNK_HEADER_SIZE]);
                toWrite = t->c[i].size - OMNI_CHUNK_FLC_HEADER_SIZE - OMNI_CHUNK_HEADER_SIZE;
                if(fwrite(body, 1, toWrite, o->out) < toWrite) {
                    fprintf(stderr, "Failed to write audio data: %s\n", strerror(errno));
                    goto error2;
                }
            } else {
                body = &(t->c[i].data[OMNI_CHUNK_HEADER_SIZE]);
                toWrite = t->c[i].size - OMNI_CHUNK_HEADER_SIZE;
                if(fwrite(body, 1, toWrite, o->out) < toWrite) {
                    fprintf(stderr, "Failed to write audio data: %s\n", strerror(errno));
                    goto error2;
                }

                if(o->trackType == OMNI_TRACK_TYPE_WAVE) {
                    o->wav.dataSize += toWrite;
                }
            }

            printf("%d: %d %d %d\n", i, o->trackNum, toWrite, t->c[i].timestamp);
        }
    }

    for(i = 0; i < t->mxobs; i++) {
        if(t->mxob[i].trackType == OMNI_TRACK_TYPE_WAVE) {
            t->mxob[i].wav.fileSize = t->mxob[i].wav.dataSize + WAV_FILE_SIZE_ADD;

            if(fseeko(t->mxob[i].out, WAV_DATA_SIZE_OFFSET, SEEK_SET) < 0) {
                fprintf(stderr, "Failed to seek to WAV data size offset.\n");
                goto error2;
            }
            if(fwrite(&(t->mxob[i].wav.dataSize), 1, sizeof(int), t->mxob[i].out) < sizeof(int)) {
                fprintf(stderr, "Failed to write WAV data size.\n");
                goto error2;
            }
            if(fseeko(t->mxob[i].out, WAV_FILE_SIZE_OFFSET, SEEK_SET) < 0) {
                fprintf(stderr, "Failed to seek to WAV file size offset.\n");
                goto error2;
            }
            if(fwrite(&(t->mxob[i].wav.fileSize), 1, sizeof(int), t->mxob[i].out) < sizeof(int)) {
                fprintf(stderr, "Failed to write WAV file size.\n");
                goto error2;
            }
        }

        if(t->mxob[i].out != NULL) {
            fclose(t->mxob[i].out);
        } else if(t->mxob[i].trackType != OMNI_TRACK_TYPE_MUXED) {
            fprintf(stderr, "%s with track number %d never had any packets.\n",
                            t->mxob[i].trackName, t->mxob[i].trackNum);
        }
    }

    free(t->c);
    free(t->mxob);

    return(0);

error2:
    for(i = 0; i < t->mxobs; i++) {
        if(t->mxob[i].out != NULL) {
            fclose(t->mxob[i].out);
        }
    }
    free(t->c);
error1:
    free(t->mxob);
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
        if(riff_traverse(r, "MxStMxSt", dump_song_cb, &t) < 0) {
            fprintf(stderr, "Failed to traverse file.\n");
        }
    }

    riff_close(r);

    exit(EXIT_SUCCESS);
}
