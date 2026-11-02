#include "text.h"
#include <stdint.h>

/* Original 5x7 bitmap glyphs. Fixed 2x pixels allow a usable small terminal
 * without requiring a kernel console glyph ABI or framebuffer ownership. */
struct glyph {char c;uint8_t rows[7];};
static const struct glyph glyphs[]={
 {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
 {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
 {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
 {'G',{14,17,16,23,17,17,14}}, {'H',{17,17,17,31,17,17,17}},
 {'I',{31,4,4,4,4,4,31}}, {'J',{7,2,2,2,18,18,12}},
 {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
 {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
 {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
 {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
 {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
 {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
 {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
 {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
 {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
 {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
 {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
 {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
 {':',{0,4,4,0,4,4,0}}, {'.',{0,0,0,0,0,12,12}},
 {'/',{1,1,2,4,8,16,16}}, {'-',{0,0,0,31,0,0,0}},
 {'=',{0,31,0,0,31,0,0}}, {'_',{0,0,0,0,0,0,31}},
 {'(',{2,4,8,8,8,4,2}}, {')',{8,4,2,2,2,4,8}},
 {'?',{14,17,1,2,4,0,4}}, {'!',{4,4,4,4,4,0,4}},
 {'+', {0,4,4,31,4,4,0}}, {'#',{10,31,10,10,31,10,0}},
};
static const uint8_t *rows_for(char c) {
    if(c>='a' && c<='z')c=(char)(c-'a'+'A');
    for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++)
        if(glyphs[i].c==c)return glyphs[i].rows;
    return 0;
}
void nasd_draw_text(uint8_t *pixels,unsigned width,unsigned height,
                    unsigned left,unsigned top,const char *input,size_t length,
                    uint8_t r,uint8_t g,uint8_t b) {
    if(!pixels || !input || left>=width || top>=height)return;
    unsigned x=left,y=top;
    for(size_t k=0;k<length;k++) {
        char c=input[k];
        if(c=='\r')continue;
        if(c=='\n' || x+10>=width) {
            x=left;y+=18;
            if(c=='\n')continue;
        }
        if(y+14>=height)return;
        const uint8_t *rows=rows_for(c);
        if(rows)for(unsigned gy=0;gy<7;gy++)
            for(unsigned gx=0;gx<5;gx++)
                if(rows[gy]&(1U<<(4-gx)))
                    for(unsigned dy=0;dy<2;dy++)
                        for(unsigned dx=0;dx<2;dx++) {
                            unsigned px=x+2*gx+dx,py=y+2*gy+dy;
                            if(px>=width || py>=height)continue;
                            size_t p=((size_t)py*width+px)*4U;
                            pixels[p]=b;pixels[p+1]=g;
                            pixels[p+2]=r;pixels[p+3]=255;
                        }
        x+=12;
    }
}
