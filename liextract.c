#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "riff.h"

#define SHORT_FROM_ARRAY(ARRAY, INDEX) (*(short int *)(&((ARRAY)[(INDEX)])))
#define INT_FROM_ARRAY(ARRAY, INDEX) (*(int *)(&((ARRAY)[(INDEX)])))

#define OMNI_TRACK_TYPE_AUDIO (4)
#define OMNI_TRACK_TYPE_VIDEO (7)

#define OMNI_CHUNK_HEADER_SIZE (14)

#define OMNI_CHUNK_TYPE_DATA (0)
#define OMNI_CHUNK_TYPE_PARTIAL (16)
#define OMNI_CHUNK_TYPE_LAST (2)

char WAVHeader[] = {'R', 'I', 'F', 'F', /* 4 */
                    0  , 0  , 0  , 0  , /* 8 file size following */
                    'W', 'A', 'V', 'E', /* 12 */
                    'f', 'm', 't', ' ', /* 16 */
                    16 , 0  , 0  , 0  , /* 20 always 16 in this case */
                    0  , 0  ,           /* 22 */
                    0  , 0  ,           /* 24 */
                    0  , 0  , 0  , 0  , /* 28 */
                    0  , 0  , 0  , 0  , /* 32 */
                    0  , 0  ,           /* 34 */
                    0  , 0  ,           /* 36 */
                    'd', 'a', 't', 'a', /* 40 */
                    0  , 0  , 0  , 0  };/* 44 */ /* file size following */

#define WAV_FILE_SIZE_OFFSET    (4)
#define WAV_FORMAT_OFFSET       (20)
#define WAV_CHANNELS_OFFSET     (22)
#define WAV_SAMPLERATE_OFFSET   (24)
#define WAV_BYTESSEC_OFFSET     (28)
#define WAV_BYTESSAMP_OFFSET    (32)
#define WAV_BITSSAMP_OFFSET     (34)
#define WAV_DATA_SIZE_OFFSET    (40)

#define WAV_HEADER_SIZE         (36)    /* offset to add for total file size */

typedef struct {
    unsigned int size;
    unsigned char data[22064];
    unsigned char *audio;

    /* required fields */
    short int chunkType;
    int trackNum;
    int unk0;
    int hdrSize;
} Chunk;

typedef struct {
    char MxOb[1024];
    int index;

    /* MxOb data */
    char *trackName;
    int trackType;
    int trackNum;
    int unk0;
    int unk1;
    int unk2;
    int unk3;
    int unk4;
    char *fileName;
    char format[4];
    int unk5;

    /* fields from first chunk */
    /* WAV fmt header */
    short int fmtType;
    short int fmtChans;
    int fmtRate;
    int fmtBytesPerSecond;
    short int fmtBytesPerSample;
    short int fmtBitsPerSample;

    int hdrDataSize;

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

int get_track_info_cb(RIFFFile *r, int dir, int ent, void *priv) {
    int index = RIFF_ENTRY(r, dir, ent);
    Track *t = priv;
    int dataPos;

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

    t->trackType = INT_FROM_ARRAY(t->MxOb, 0);

    t->trackName = &(t->MxOb[7]);
    dataPos = 7 + strlen(t->trackName) + 1;

    t->trackNum = INT_FROM_ARRAY(t->MxOb, dataPos);
    t->unk0 = INT_FROM_ARRAY(t->MxOb, dataPos + 4);
    t->unk1 = INT_FROM_ARRAY(t->MxOb, dataPos + 12);
    t->unk2 = INT_FROM_ARRAY(t->MxOb, dataPos + 16);
    t->unk3 = INT_FROM_ARRAY(t->MxOb, dataPos + 66);
    t->unk4 = INT_FROM_ARRAY(t->MxOb, dataPos + 82);
    t->fileName = &(t->MxOb[dataPos + 94]);
    dataPos += 94 + strlen(t->fileName) + 1;
    memcpy(t->format, &(t->MxOb[dataPos + 12]), sizeof(t->format));
    t->unk5 = INT_FROM_ARRAY(t->MxOb, dataPos + 24);

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
    c->unk0 = INT_FROM_ARRAY(c->data, 6);
    c->hdrSize = INT_FROM_ARRAY(c->data, 10);

    return(0);
}

int dump_song_cb(RIFFFile *r, int dir, int ent, void *priv) {
    Track *t = priv;
    t->index = RIFF_ENTRY(r, dir, ent);
    unsigned int i;
    FILE *outFile;
    unsigned int toWrite, written;

    t->trackType = 0;
    t->chunks = 0;
    t->c = NULL;

    if(do_traverse(r, "MxOb", get_track_info_cb, t, 0, t->index) < 0) {
        return(-1);
    }

    if(t->trackType == 0) {
        fprintf(stderr, "Failed to find MxOb.\n");
        return(-1);
    }

    fprintf(stderr, "Name: %s\n"
                    "Index: %d\n"
                    "Type: %d\n"
                    "Track num: %d\n"
                    "Unknown 0: %d\n"
                    "Unknown 1: %d\n"
                    "Unknown 2: %d\n"
                    "Unknown 3: %08X\n"
                    "Unknown 4: %08X\n",
                    t->trackName,
                    t->index,
                    t->trackType,
                    t->trackNum,
                    t->unk0,
                    t->unk1,
                    t->unk2,
                    t->unk3,
                    t->unk4);

    if(t->trackType != OMNI_TRACK_TYPE_AUDIO) {
        fprintf(stderr, "Video tracks aren't supported.\n\n");
        return(0);
    }

    fprintf(stderr, "File name: %s\n", t->fileName);
    fprintf(stderr, "Unknown 5: %d\n",
                    t->unk5);

    if(do_traverse(r, "MxDaMxCh", read_chunks_cb, t, 0, t->index) < 0) {
        return(-1);
    }

    fprintf(stderr, "read %d chunks.\n", t->chunks);

    outFile = fopen(t->trackName, "wb");
    if(outFile == NULL) {
        fprintf(stderr, "Failed to open file %s for writing.\n", t->trackName);
        return(-1);
    }

    written = 0;
    for(i = 0; i < t->chunks; i++) {
        t->c[i].audio = &(t->c[i].data[OMNI_CHUNK_HEADER_SIZE]);
        if(i == 0) {
            t->fmtType = SHORT_FROM_ARRAY(t->c[0].audio, 0);
            t->fmtChans = SHORT_FROM_ARRAY(t->c[0].audio, 2);
            t->fmtRate = INT_FROM_ARRAY(t->c[0].audio, 4);
            t->fmtBytesPerSecond = INT_FROM_ARRAY(t->c[0].audio, 8);
            t->fmtBytesPerSample = SHORT_FROM_ARRAY(t->c[0].audio, 12);
            t->fmtBitsPerSample = SHORT_FROM_ARRAY(t->c[0].audio, 14);
            INT_FROM_ARRAY(WAVHeader, WAV_FILE_SIZE_OFFSET) = -1;
            SHORT_FROM_ARRAY(WAVHeader, WAV_FORMAT_OFFSET) = t->fmtType;
            SHORT_FROM_ARRAY(WAVHeader, WAV_CHANNELS_OFFSET) = t->fmtChans;
            INT_FROM_ARRAY(WAVHeader, WAV_SAMPLERATE_OFFSET) = t->fmtRate;
            INT_FROM_ARRAY(WAVHeader, WAV_BYTESSEC_OFFSET) = t->fmtBytesPerSecond;
            SHORT_FROM_ARRAY(WAVHeader, WAV_BYTESSAMP_OFFSET) = t->fmtBytesPerSample;
            SHORT_FROM_ARRAY(WAVHeader, WAV_BITSSAMP_OFFSET) = t->fmtBitsPerSample;
            INT_FROM_ARRAY(WAVHeader, WAV_DATA_SIZE_OFFSET) = -1;
            if(fwrite(WAVHeader, 1, sizeof(WAVHeader), outFile) < sizeof(WAVHeader)) {
                fprintf(stderr, "Failed to write WAV header.\n");
                return(-1);
            }
        } else {
            if(t->c[i].chunkType != OMNI_CHUNK_TYPE_LAST) {
                toWrite = t->c[i].size - OMNI_CHUNK_HEADER_SIZE;
                if(fwrite(t->c[i].audio, 1, toWrite, outFile) < toWrite) {
                    fprintf(stderr, "Failed to write audio data: %s\n", strerror(errno));
                    return(-1);
                }
                written += toWrite;
            }
        }
    }
    if(fseeko(outFile, WAV_DATA_SIZE_OFFSET, SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to data header.\n");
        return(-1);
    }
    if(fwrite(&written, 1, sizeof(int), outFile) < sizeof(int)) {
        fprintf(stderr, "Failed to write data size.\n");
        return(-1);
    }
    written += WAV_HEADER_SIZE;
    if(fseeko(outFile, WAV_FILE_SIZE_OFFSET, SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to WAV header.\n");
        return(-1);
    }
    if(fwrite(&written, 1, sizeof(int), outFile) < sizeof(int)) {
        fprintf(stderr, "Failed to write file size.\n");
        return(-1);
    }
    fclose(outFile);
    fprintf(stderr, "Wrote to %s.\n", t->trackName);

    free(t->c);

    return(0);
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
