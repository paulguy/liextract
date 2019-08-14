#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "riff.h"

/* minimum value necessary to get a list of all SIs */
#define BRUTE_ISENTRY_TRIES     (16)
#define CHUNK_MINIMUM_SIZE      (12)

const short int OMNI_TRACK_TYPE_MUXED[] = {6, 7, 9};

const char RIFFMagic[4] = {'R', 'I', 'F', 'F'};
const char LISTFourCC[4] = {'L', 'I', 'S', 'T'};
const char MxObFourCC[4] = {'M', 'x', 'O', 'b'};
const char MxStFourCC[4] = {'M', 'x', 'S', 't'};
const char MxChFourCC[4] = {'M', 'x', 'C', 'h'};
const char Nodes[][4] = 
    {
        {'R', 'I', 'F', 'F'}, /* root entry */
        {'L', 'I', 'S', 'T'},
        {'M', 'x', 'S', 't'}
    };

const char Leaves[][4] =
    {
        {'M', 'x', 'H', 'd'},
        {'M', 'x', 'O', 'f'},
        {'p', 'a', 'd', ' '},
        {'M', 'x', 'C', 'h'},
        {'M', 'x', 'O', 'b'}
    };

int isRIFF(char fourCC[4]) {
    if(!memcmp(fourCC, RIFFMagic, sizeof(RIFFMagic))) {
        return(1);
    }

    return(0);
}

int isLIST(char fourCC[4]) {
    if(!memcmp(fourCC, LISTFourCC, sizeof(LISTFourCC))) {
        return(1);
    }

    return(0);
}

int isNode(char fourCC[4]) {
    unsigned int i;

    for(i = 0; i < sizeof(Nodes) / sizeof(Nodes[0]); i++) {
        if(!memcmp(fourCC, Nodes[i], sizeof(Nodes[i]))) {
            return(1);
        }
    }

    return(0);
}

int isLeaf(char fourCC[4]) {
    unsigned int i;

    for(i = 0; i < sizeof(Leaves) / sizeof(Leaves[0]); i++) {
        if(!memcmp(fourCC, Leaves[i], sizeof(Leaves[i]))) {
            return(1);
        }
    }

    return(0);
}

int isEntry(char fourCC[4]) {
    if(isNode(fourCC) || isLeaf(fourCC) ||
       !memcmp(fourCC, MxObFourCC, sizeof(MxObFourCC))) {
        return(1);
    }

    return(0);
}

int isMuxed(short int type) {
    unsigned int i;

    for(i = 0; i < sizeof(OMNI_TRACK_TYPE_MUXED) / sizeof(OMNI_TRACK_TYPE_MUXED[0]); i++) {
        if(type == OMNI_TRACK_TYPE_MUXED[i]) {
            return(1);
        }
    }

    return(0);
}

RIFFFile *riff_init() {
    RIFFFile *r;

    r = malloc(sizeof(RIFFFile));
    if(r == NULL) {
        fprintf(stderr, "Failed to allocate memory for RIFFFile.\n");
        return(NULL);
    }
    r->f = NULL;
    r->root = NULL;
    r->entryMemCount = 0;
    
    return(r);
}

int riff_grow(RIFFFile *r) {
    RIFFEntry *e;

    e = realloc(r->root, sizeof(RIFFEntry) * (r->entryMemCount + 1));
    if(e == NULL) {
        fprintf(stderr, "Failed to allocate memory to grow entry table.\n");
        return(-1);
    }

    r->root = e;
    r->entryMemCount++;

    return(r->entryMemCount-1);
}

off_t riff_entry_offset(RIFFFile *r, int index) {
    off_t pos = 0;

    while(index != -1) {
        pos += r->root[index].start;
        index = r->root[index].parent;
    }

    return(pos);
}

int riff_entry_seekto(RIFFFile *r, int index) {
    if(fseeko(r->f, riff_entry_offset(r, index), SEEK_SET) < 0) {
        return(-1);
    }

    return(0);
}

/* probably some alignment rules i'm not familiar with, just brute force it */
int brute_isEntry(FILE *f, char *fourCC, off_t *pos, int tries) {
    for(; tries > 0; tries--) {
        if(fread(fourCC, 1, 4, f) < 4) {
            fprintf(stderr, "Failed to read entry fourCC.\n");
            return(-1);
        }
        (*pos)++;

        if(isEntry(fourCC)) {
            (*pos) += 3;
            return(1);
        }

        if(fseeko(f, -3, SEEK_CUR) < 0) {
            fprintf(stderr, "Failed to seek back to entry.\n");
            return(-1);
        }
    }

    return(0);
}

int riff_populate(RIFFFile *r, int index, int depth) {
    char fourCC[4];
    off_t pos = 0;
    int cur;
    int i;
    int ret;
    unsigned int MxObSize;
    unsigned short int unkNameSize;
    short int MxObType = 0;

    if(!isNode(r->root[index].fourCC)) {
        return(0);
    }

    if(riff_entry_seekto(r, index) < 0) {
        fprintf(stderr, "Failed to seek to entry.\n");
        return(-1);
    }

    while(pos < r->root[index].size - CHUNK_MINIMUM_SIZE) {
        ret = brute_isEntry(r->f, fourCC, &pos, BRUTE_ISENTRY_TRIES);
        if(ret == 0) {
            fprintf(stderr, "Unknown fourCC %08X near %ld\n",
                    *((unsigned int *)fourCC),
                    riff_entry_offset(r, index) + pos - BRUTE_ISENTRY_TRIES);
            return(-1);
        } else if(ret > 0) {
            /* bunch of annoying stuff to forge a muxed MxOb */
            if(fread(&MxObSize, 1, sizeof(int), r->f) < sizeof(int)) {
                fprintf(stderr, "Failed to read entry size.\n");
                return(-1);
            }
            if(fread(&MxObType, 1, sizeof(short int), r->f) < sizeof(short int)) {
                fprintf(stderr, "Failed to read MxOb type.\n");
                return(-1);
            }
            if(!memcmp(fourCC, MxObFourCC, sizeof(MxObFourCC)) &&
               isMuxed(MxObType)) {
                /* get an MxOb */
                cur = riff_grow(r);
                if(cur == -1) {
                    return(-1);
                }
                if(r->root[index].entry == -1) {
                    r->root[index].entry = cur;
                }

                memcpy(&(r->root[cur].fourCC), MxObFourCC, sizeof(r->root[cur].fourCC));
                r->root[cur].entries = 0;
                r->root[cur].entry = -1;
                r->root[cur].parent = index;
                r->root[cur].start = pos + 4;

                /* If there's a string directly after the value, keep reading. */
                pos += 6;
                while(pos < r->root[index].size) {
                    pos++;
                    if(fgetc(r->f) == 0) {
                        break;
                    }
                }
                /* find the start of the name */
                while(pos < r->root[index].size) {
                    pos++;
                    if(fgetc(r->f) != 0) {
                        break;
                    }
                }
                /* keep consuming the name too */
                while(pos < r->root[index].size) {
                    pos++;
                    if(fgetc(r->f) == 0) {
                        break;
                    }
                }

                /* seek past fixed-sized structure */
                if(fseeko(r->f, 92, SEEK_CUR) < 0) {
                    fprintf(stderr, "Failed to seek forward.\n");
                    return(-1);
                }
                pos += 92;

                if(fread(&unkNameSize, 1, sizeof(short int), r->f) < sizeof(short int)) {
                    fprintf(stderr, "Failed to read unknown name size.\n");
                    return(-1);
                }
                pos += 2;
                /* seek past string */
                if(fseeko(r->f, unkNameSize, SEEK_CUR) < 0) {
                    fprintf(stderr, "Failed to seek forward.\n");
                    return(-1);
                }
                pos += unkNameSize;

                r->root[cur].size = pos - r->root[cur].start;
                r->root[index].entries++;
            } else {
                if(fseeko(r->f, -6, SEEK_CUR) < 0) {
                    fprintf(stderr, "Failed to seek back to entry.\n");
                    return(-1);
                }

                cur = riff_grow(r);
                if(cur == -1) {
                    return(-1);
                }
                if(r->root[index].entry == -1) {
                    r->root[index].entry = cur;
                }

                memcpy(&(r->root[cur].fourCC), fourCC, sizeof(r->root[cur].fourCC));
                r->root[cur].entries = 0;
                r->root[cur].entry = -1;
                r->root[cur].parent = index;

                if(fread(&(r->root[cur].size), 1, sizeof(int), r->f) < sizeof(int)) {
                    fprintf(stderr, "Failed to read entry size.\n");
                    return(-1);
                }
                pos += 4;
                if(isLIST(fourCC)) {
                    if(fread(r->root[cur].fourCC2, 1,
                       sizeof(r->root[cur].fourCC2), r->f) < sizeof(r->root[cur].fourCC2)) {
                        fprintf(stderr, "Failed to read entry second fourCC.\n");
                        return(-1);
                    }
                    pos += 4;
                    r->root[cur].size -= 4;

                    /* MxCh LISTs have a count of items in them */
                    if(!memcmp(r->root[cur].fourCC2, MxChFourCC, sizeof(MxChFourCC))) {
                        if(fseeko(r->f, 4, SEEK_CUR) < 0) {
                            fprintf(stderr, "Failed to seek past count.\n");
                            return(-1);
                        }
                        pos += 4;
                        r->root[cur].size -= 4;
                    }
                }

                r->root[cur].start = pos;

                if(fseeko(r->f, r->root[cur].size, SEEK_CUR) < 0) {
                    fprintf(stderr, "Failed to seek to next entry.\n");
                    return(-1);
                }
                pos += r->root[cur].size;

                r->root[index].entries++;
            }
        } else { /* error reading fourCC */
            return(-1);
        }
    }

    for(i = 0; i < r->root[index].entries; i++) {
        if(riff_populate(r, r->root[index].entry + i, depth + 1) < 0) {
            return(-1);
        }
    }

    return(r->root[index].entries);
}

RIFFFile *riff_open(const char *filename) {
    RIFFFile *r;
    FILE *f;
    char magic[4];
    int size;

    f = fopen(filename, "rb");
    if(f == NULL) {
        fprintf(stderr, "Failed to open SI file for reading.\n");
        goto error0;
    }
    if(fread(magic, 1, 4, f) < 4) {
        fprintf(stderr, "Failed to read magic.\n");
        goto error1;
    }
    if(!isRIFF(magic)) {
        fprintf(stderr, "File is not a RIFF file.\n");
        goto error1;
    }
    r = riff_init();
    if(r == NULL) {
        goto error1;
    }
    if(fread(&size, 1, sizeof(int), f) < sizeof(int)) {
        fprintf(stderr, "Failed to read size.\n");
        goto error2;
    }

    r->f = f;
    if(riff_grow(r) < 0) {
        goto error2;
    }
    memcpy(r->root->fourCC, magic, sizeof(r->root->fourCC));
    if(fread(r->root->fourCC2, 1, sizeof(r->root->fourCC2), f) < sizeof(r->root->fourCC2)) {
        fprintf(stderr, "Failed to read fourCC.\n");
        goto error3;
    }
    r->root->start = 12; /* start after header */
    r->root->size = size - 4; /* cut out file fourCC */
    r->root->entries = 0;
    r->root->entry = -1;
    r->root->parent = -1;

    if(riff_populate(r, 0, 0) < 0) {
        goto error3;
    }

    return(r);

error3:
    if(riff_traverse(r, "", print_entry_cb, NULL) < 0) {
        fprintf(stderr, "Failed to traverse file.\n");
    }
    free(r->root);
error2:
    free(r);
error1:
    fclose(f);
error0:
    return(NULL);
}

void riff_free(RIFFFile *r) {
    if(r->root != NULL) {
        free(r->root);
    }
    free(r);
}

void riff_close(RIFFFile *r) {
    fclose(r->f);
    riff_free(r);
}

int do_traverse(RIFFFile *r,
                const char *pattern,
                int (*match_cb)(RIFFFile *r, int dir, int ent, void *priv),
                void *priv,
                int matchAll,
                int dir) {
    int index = r->root[dir].entry;
    int i;
    const char *fourCC;
    int ret;

    for(i = 0; i < r->root[dir].entries; i++) {
        if(isNode(r->root[index + i].fourCC)) {
            if(matchAll) {
                ret = match_cb(r, dir, i, priv);
                if(ret) return(ret);
                ret = do_traverse(r, NULL, match_cb, priv, matchAll, index + i);
                if(ret) return(ret);
            } else {
                if(isLIST(r->root[index + i].fourCC)) {
                    fourCC = r->root[index + i].fourCC2;
                } else {
                    fourCC = r->root[index + i].fourCC;
                }
                if(!memcmp(pattern, fourCC, 4)) {
                     /* no sense recursing if we'd be passing an empty string as
                     the match may be a directory. */
                    if(pattern[4] == '\0') {
                        ret = match_cb(r, dir, i, priv);
                        if(ret) return(ret);
                    } else {
                        ret = do_traverse(r, &(pattern[4]), match_cb, priv, matchAll, index + i);
                        if(ret) return(ret);
                    }
                }
            }
        } else {
            if(matchAll || !memcmp(pattern, r->root[index + i].fourCC, 4)) {
                ret = match_cb(r, dir, i, priv);
                if(ret) return(ret);
            }
        }
    }

    return(0);
}

int riff_traverse(RIFFFile *r,
                  const char *pattern,
                  int (*match_cb)(RIFFFile *r, int dir, int ent, void *priv),
                  void *priv) {
    int matchAll = 0;

    if(strlen(pattern) % 4) {
        fprintf(stderr, "Pattern must be mulitple of 4 chars.");
        return(-1);
    }

    if(strlen(pattern) == 0) {
        matchAll = 1;
    }

    return(do_traverse(r, pattern, match_cb, priv, matchAll, 0));
}

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

