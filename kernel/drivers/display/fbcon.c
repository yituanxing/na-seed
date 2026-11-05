/* SPDX-License-Identifier: GPL-2.0-only */
/* Linux VT1's framebuffer text plane. It is a tiny fbcon personality over
 * the existing DisplayDevice/fbdev owner, not another GPU or an fb mmap.
 * Rendering occurs from task/scheduler context, never the physical IRQ. */
#include "fbcon.h"
#include "fbdev.h"
#include "../../errno.h"
#include "../../lib/string.h"

#define FBCON_COLS 80U
#define FBCON_ROWS 25U
#define FBCON_CELL_W 8U
#define FBCON_CELL_H 16U
#define FBCON_PIXEL_W (FBCON_COLS*FBCON_CELL_W)
#define FBCON_PIXEL_H (FBCON_ROWS*FBCON_CELL_H)
#define FBCON_MAX_WIDTH 1920U
#define FBCON_CSI_PARAMS 8U

/* The kernel owns only glyphs, attributes and cursor state. This 5x7 data
 * uses the same readable bitmap scheme as the older user desktop renderer,
 * with true lower-case and punctuation for real Alpine login and ash. */
struct fbcon_glyph {char c;uint8_t row[7];};
static const struct fbcon_glyph font[]={
 {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},
 {'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},
 {'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},
 {'G',{14,17,16,23,17,17,14}},{'H',{17,17,17,31,17,17,17}},
 {'I',{31,4,4,4,4,4,31}},{'J',{7,2,2,2,18,18,12}},
 {'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
 {'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},
 {'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
 {'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},
 {'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},
 {'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},
 {'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
 {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
 {'a',{0,0,14,1,15,17,15}},{'b',{16,16,22,25,17,17,30}},
 {'c',{0,0,14,17,16,17,14}},{'d',{1,1,13,19,17,17,15}},
 {'e',{0,0,14,17,31,16,14}},{'f',{6,9,8,28,8,8,8}},
 {'g',{0,15,17,17,15,1,14}},{'h',{16,16,22,25,17,17,17}},
 {'i',{4,0,12,4,4,4,14}},{'j',{2,0,6,2,2,18,12}},
 {'k',{16,16,18,20,24,20,18}},{'l',{12,4,4,4,4,4,14}},
 {'m',{0,0,26,21,21,21,21}},{'n',{0,0,22,25,17,17,17}},
 {'o',{0,0,14,17,17,17,14}},{'p',{0,0,30,17,30,16,16}},
 {'q',{0,0,13,19,15,1,1}},{'r',{0,0,22,25,16,16,16}},
 {'s',{0,0,15,16,14,1,30}},{'t',{8,8,28,8,8,9,6}},
 {'u',{0,0,17,17,17,19,13}},{'v',{0,0,17,17,17,10,4}},
 {'w',{0,0,17,17,21,21,10}},{'x',{0,0,17,10,4,10,17}},
 {'y',{0,0,17,17,15,1,14}},{'z',{0,0,31,2,4,8,31}},
 {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},
 {'2',{14,17,1,2,4,8,31}},{'3',{30,1,1,14,1,1,30}},
 {'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},
 {'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,1,14}},
 {'!',{4,4,4,4,4,0,4}},{'"',{10,10,10,0,0,0,0}},
 {'#',{10,31,10,10,31,10,0}},{'$',{4,15,20,14,5,30,4}},
 {'%',{25,25,2,4,8,19,19}},{'&',{12,18,20,8,21,18,13}},
 {'\'',{4,4,8,0,0,0,0}},{'(',{2,4,8,8,8,4,2}},
 {')',{8,4,2,2,2,4,8}},{'*',{0,21,14,31,14,21,0}},
 {'+',{0,4,4,31,4,4,0}},{',',{0,0,0,0,6,6,4}},
 {'-',{0,0,0,31,0,0,0}},{'.',{0,0,0,0,0,12,12}},
 {'/',{1,1,2,4,8,16,16}},{':',{0,4,4,0,4,4,0}},
 {';',{0,4,4,0,4,4,8}},{'<',{2,4,8,16,8,4,2}},
 {'=',{0,31,0,0,31,0,0}},{'>',{8,4,2,1,2,4,8}},
 {'?',{14,17,1,2,4,0,4}},{'@',{14,17,23,21,23,16,14}},
 {'[',{14,8,8,8,8,8,14}},{'\\',{16,16,8,4,2,1,1}},
 {']',{14,2,2,2,2,2,14}},{'^',{4,10,17,0,0,0,0}},
 {'_',{0,0,0,0,0,0,31}},{'`',{8,4,2,0,0,0,0}},
 {'{',{3,4,4,24,4,4,3}},{'|',{4,4,4,4,4,4,4}},
 {'}',{24,4,4,3,4,4,24}},{'~',{0,0,9,22,0,0,0}},
};

struct fbcon_rgb {uint8_t r,g,b;};
static const struct fbcon_rgb palette[16]={
 {0,0,0},{170,0,0},{0,170,0},{170,85,0},
 {0,0,170},{170,0,170},{0,170,170},{170,170,170},
 {85,85,85},{255,85,85},{85,255,85},{255,255,85},
 {85,85,255},{255,85,255},{85,255,255},{255,255,255}
};
struct fbcon_cell {uint8_t c,fg,bg;};
static struct {
    struct fbcon_cell cells[FBCON_ROWS][FBCON_COLS];
    unsigned col,row,saved_col,saved_row;
    uint16_t params[FBCON_CSI_PARAMS];
    unsigned param_count;
    uint8_t fg,bg,parser;
    bool active,graphics,cursor,bright,private_csi;
    bool ready;
    uint32_t width,height,pitch,left,top;
    struct nas_fb_var_screeninfo mode;
} con;
static uint8_t row_pixels[FBCON_MAX_WIDTH*4U];

static const uint8_t *glyph(char c) {
    for(unsigned i=0;i<sizeof(font)/sizeof(font[0]);i++)
        if(font[i].c==c)return font[i].row;
    return NULL;
}
static uint32_t pack(struct fbcon_rgb rgb) {
    uint32_t value=((uint32_t)rgb.r<<con.mode.red.offset)|
                   ((uint32_t)rgb.g<<con.mode.green.offset)|
                   ((uint32_t)rgb.b<<con.mode.blue.offset);
    if(con.mode.transp.length && con.mode.transp.length<=8U)
        value|=((1U<<con.mode.transp.length)-1U)<<con.mode.transp.offset;
    return value;
}
static void pixel(uint8_t *at,struct fbcon_rgb rgb) {
    uint32_t color=pack(rgb);
    at[0]=(uint8_t)color;at[1]=(uint8_t)(color>>8);
    at[2]=(uint8_t)(color>>16);at[3]=(uint8_t)(color>>24);
}
static bool configure(void) {
    struct nas_fb_fix_screeninfo fix;
    if(!nas_fbdev_var_screeninfo(&con.mode) ||
       !nas_fbdev_fix_screeninfo(&fix) ||
       con.mode.bits_per_pixel!=32U ||
       con.mode.xres<FBCON_PIXEL_W || con.mode.yres<FBCON_PIXEL_H ||
       con.mode.xres>FBCON_MAX_WIDTH ||
       fix.line_length<con.mode.xres*4U ||
       con.mode.red.length!=8U || con.mode.green.length!=8U ||
       con.mode.blue.length!=8U ||
       con.mode.red.offset>24U || con.mode.green.offset>24U ||
       con.mode.blue.offset>24U)return false;
    con.width=con.mode.xres;con.height=con.mode.yres;
    con.pitch=fix.line_length;
    con.left=(con.width-FBCON_PIXEL_W)/2U;
    con.top=(con.height-FBCON_PIXEL_H)/2U;
    return true;
}
static bool visible(void) {return con.active && !con.graphics && con.ready;}
static bool cursor_at(unsigned x,unsigned y) {
    return con.cursor && con.col==x && con.row==y;
}
static void cell_line(uint8_t *dest,const struct fbcon_cell *c,
                      unsigned subrow,bool reversed) {
    uint8_t fg=c->fg,bg=c->bg;
    if(reversed){uint8_t tmp=fg;fg=bg;bg=tmp;}
    const uint8_t *shape=glyph((char)c->c);
    uint8_t bits=shape && subrow>=1U && subrow<=14U?
                 shape[(subrow-1U)/2U]:0U;
    for(unsigned x=0;x<FBCON_CELL_W;x++) {
        bool ink=x>=1U&&x<=5U && (bits&(1U<<(5U-x)))!=0;
        pixel(dest+x*4U,palette[ink?fg:bg]);
    }
}
static void paint_cell(unsigned x,unsigned y,bool show_cursor) {
    if(!visible())return;
    const struct fbcon_cell *c=&con.cells[y][x];
    uint64_t left=(uint64_t)con.left+(uint64_t)x*FBCON_CELL_W;
    uint64_t top=(uint64_t)con.top+(uint64_t)y*FBCON_CELL_H;
    for(unsigned sub=0;sub<FBCON_CELL_H;sub++) {
        cell_line(row_pixels,c,sub,show_cursor&&cursor_at(x,y));
        (void)nas_fbdev_write_at((top+sub)*con.pitch+left*4U,
                                 row_pixels,FBCON_CELL_W*4U);
    }
}
static void paint_all(void) {
    if(!visible())return;
    for(unsigned y=0;y<con.height;y++) {
        memset(row_pixels,0,con.width*4U);
        if(y>=con.top && y<con.top+FBCON_PIXEL_H) {
            unsigned cell_row=(y-con.top)/FBCON_CELL_H;
            unsigned subrow=(y-con.top)%FBCON_CELL_H;
            for(unsigned x=0;x<FBCON_COLS;x++)
                cell_line(row_pixels+((con.left+x*FBCON_CELL_W)*4U),
                          &con.cells[cell_row][x],subrow,cursor_at(x,cell_row));
        }
        (void)nas_fbdev_write_at((uint64_t)y*con.pitch,
                                 row_pixels,con.width*4U);
    }
}
static void blank_cell(unsigned x,unsigned y) {
    con.cells[y][x]=(struct fbcon_cell){' ',con.fg,con.bg};
}
static void clear_all(void) {
    for(unsigned y=0;y<FBCON_ROWS;y++)
        for(unsigned x=0;x<FBCON_COLS;x++)blank_cell(x,y);
}
static void scroll_up(void) {
    memmove(con.cells[0],con.cells[1],
            sizeof(con.cells)-sizeof(con.cells[0]));
    for(unsigned x=0;x<FBCON_COLS;x++)blank_cell(x,FBCON_ROWS-1U);
    paint_all();
}
static void newline(void) {
    con.col=0;
    if(con.row+1U<FBCON_ROWS)con.row++;
    else scroll_up();
}
static unsigned one(unsigned i) {
    return i<con.param_count && con.params[i]?con.params[i]:1U;
}
static unsigned param(unsigned i) {
    return i<con.param_count?con.params[i]:0U;
}
static void cursor_move(unsigned x,unsigned y) {
    con.col=x<FBCON_COLS?x:FBCON_COLS-1U;
    con.row=y<FBCON_ROWS?y:FBCON_ROWS-1U;
}
static void sgr(unsigned value) {
    if(value==0U){con.fg=15U;con.bg=0U;con.bright=false;}
    else if(value==1U){con.bright=true;if(con.fg<8U)con.fg+=8U;}
    else if(value==22U){con.bright=false;if(con.fg>=8U)con.fg-=8U;}
    else if(value==39U)con.fg=15U;
    else if(value==49U)con.bg=0U;
    else if(value>=30U&&value<=37U)
        con.fg=(uint8_t)(value-30U+(con.bright?8U:0U));
    else if(value>=40U&&value<=47U)con.bg=(uint8_t)(value-40U);
    else if(value>=90U&&value<=97U)con.fg=(uint8_t)(value-90U+8U);
    else if(value>=100U&&value<=107U)con.bg=(uint8_t)(value-100U+8U);
}
static void erase_row(unsigned y,unsigned first,unsigned end) {
    if(first>FBCON_COLS)first=FBCON_COLS;
    if(end>FBCON_COLS)end=FBCON_COLS;
    for(unsigned x=first;x<end;x++) {
        blank_cell(x,y);
        paint_cell(x,y,false);
    }
}
static void execute_csi(uint8_t final) {
    unsigned n=one(0);
    switch(final) {
    case 'm':
        for(unsigned i=0;i<con.param_count;i++)sgr(param(i));
        break;
    case 'H':case 'f':cursor_move(one(1)-1U,one(0)-1U);break;
    case 'A':con.row=con.row<n?0U:con.row-n;break;
    case 'B':con.row=(con.row+n<FBCON_ROWS)?con.row+n:FBCON_ROWS-1U;break;
    case 'C':con.col=(con.col+n<FBCON_COLS)?con.col+n:FBCON_COLS-1U;break;
    case 'D':con.col=con.col<n?0U:con.col-n;break;
    case 'G':cursor_move(n-1U,con.row);break;
    case 'd':cursor_move(con.col,n-1U);break;
    case 's':con.saved_col=con.col;con.saved_row=con.row;break;
    case 'u':cursor_move(con.saved_col,con.saved_row);break;
    case 'K':{
        unsigned k=param(0);
        if(k==0U)erase_row(con.row,con.col,FBCON_COLS);
        else if(k==1U)erase_row(con.row,0U,con.col+1U);
        else if(k==2U)erase_row(con.row,0U,FBCON_COLS);
        break;
    }
    case 'J':{
        unsigned k=param(0);
        if(k==2U){clear_all();paint_all();}
        else if(k==0U) {
            erase_row(con.row,con.col,FBCON_COLS);
            for(unsigned y=con.row+1U;y<FBCON_ROWS;y++)
                erase_row(y,0U,FBCON_COLS);
        } else if(k==1U) {
            for(unsigned y=0;y<con.row;y++)erase_row(y,0U,FBCON_COLS);
            erase_row(con.row,0U,con.col+1U);
        }
        break;
    }
    case 'h':case 'l':
        if(con.private_csi && param(0)==25U)con.cursor=final=='h';
        break;
    default:break;
    }
}
static void put_byte(uint8_t b) {
    if(con.parser==1U) {
        con.parser=0U;
        if(b=='[') {
            con.parser=2U;con.param_count=1U;con.params[0]=0U;
            con.private_csi=false;
        } else if(b=='7'){con.saved_col=con.col;con.saved_row=con.row;}
        else if(b=='8')cursor_move(con.saved_col,con.saved_row);
        else if(b=='D')newline();
        else if(b=='E')newline();
        else if(b=='c'){con.fg=15U;con.bg=0U;clear_all();cursor_move(0,0);paint_all();}
        return;
    }
    if(con.parser==2U) {
        if(b=='?' && con.param_count==1U && con.params[0]==0U)
            {con.private_csi=true;return;}
        if(b>='0'&&b<='9') {
            uint16_t *p=&con.params[con.param_count-1U];
            *p=*p>999U?9999U:(uint16_t)(*p*10U+(b-'0'));
            return;
        }
        if(b==';') {
            if(con.param_count<FBCON_CSI_PARAMS)
                con.params[con.param_count++]=0U;
            return;
        }
        con.parser=0U;
        if(b>=0x40U&&b<=0x7eU)execute_csi(b);
        return;
    }
    if(b==0x1bU){con.parser=1U;return;}
    if(b=='\r'){con.col=0U;return;}
    if(b=='\n'){newline();return;}
    if(b=='\b'){if(con.col)con.col--;return;}
    if(b=='\t'){unsigned next=(con.col+8U)&~7U;
        con.col=next<FBCON_COLS?next:FBCON_COLS-1U;return;}
    if(b<32U||b==127U)return;
    if(con.col>=FBCON_COLS)newline();
    con.cells[con.row][con.col]=(struct fbcon_cell){
        b<128U?b:'?',con.fg,con.bg};
    paint_cell(con.col,con.row,false);
    con.col++;
    if(con.col>=FBCON_COLS)newline();
}

void nas_fbcon_activate(bool enabled) {
    if(!enabled){con.active=false;con.graphics=false;return;}
    con.active=true;con.graphics=false;con.cursor=true;
    con.fg=15U;con.bg=0U;con.bright=false;
    con.parser=0U;con.param_count=0U;con.private_csi=false;
    con.col=con.row=con.saved_col=con.saved_row=0U;
    con.ready=configure();
    clear_all();
    paint_all();
}
void nas_fbcon_set_graphics(bool graphics) {
    if(!con.active)return;
    bool was_graphics=con.graphics;
    con.graphics=graphics;
    if(was_graphics && !graphics)paint_all();
}
uint64_t nas_fbcon_write(const uint8_t *src,uint64_t length) {
    if(!con.active || !con.ready)return (uint64_t)-ENODEV;
    if(!src && length)return (uint64_t)-EFAULT;
    if(!length)return 0U;
    if(visible())paint_cell(con.col,con.row,false); /* erase old cursor */
    for(uint64_t i=0;i<length;i++)put_byte(src[i]);
    if(visible())paint_cell(con.col,con.row,true);
    return length;
}
