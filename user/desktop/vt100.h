#ifndef NASEED_VT100_H
#define NASEED_VT100_H

/* A deliberately bounded VT100/xterm-common terminal state machine.
 * No framebuffer, evdev, PTY, kernel, or compositor dependencies live here.
 * Defining NASD_VT100_IMPLEMENTATION in exactly one translation unit provides
 * the implementation; older NaSeed app staging compiles each client as one
 * source file, and host fixtures reuse this exact implementation. */
#include <stddef.h>
#include <stdint.h>

enum { VT100_COLS=53, VT100_ROWS=25, VT100_CELL_W=6, VT100_CELL_H=9 };
struct vt100_cell { unsigned char ch,fg,bg,flags; };
struct vt100 {
    struct vt100_cell pages[2][VT100_ROWS][VT100_COLS];
    unsigned cols,rows,row,col,scroll_top,scroll_bottom;
    unsigned saved_row,saved_col,alternate,private_mode,origin_mode;
    unsigned wrap,wrapped,graphics,g0_graphics,g1_graphics,application_cursor;
    unsigned charset_select,utf8_remaining,show_cursor;
    unsigned fg,bg,flags,parse_state,parameter,parameter_count;
    unsigned params[12];
    char reply[128];
    unsigned reply_length;
};
void vt100_init(struct vt100 *,unsigned,unsigned);
void vt100_feed(struct vt100 *,const char *,size_t);
size_t vt100_take_reply(struct vt100 *,char *,size_t);
const struct vt100_cell *vt100_cell_at(const struct vt100 *,unsigned,unsigned);

#endif

#ifdef NASD_VT100_IMPLEMENTATION
#include <string.h>
#include <stdio.h>

enum { VT_NORMAL=0,VT_ESC=1,VT_CSI=2,VT_OSC=3,VT_OSC_ESC=4,
       VT_CHARSET=5,VT_IGNORE=6,VT_IGNORE_ESC=7 };
enum { VT_BOLD=1,VT_REVERSE=2,VT_UNDERLINE=4 };

static struct vt100_cell (*vt100_active(struct vt100 *v))[VT100_COLS] {
    return v->pages[v->alternate];
}
static void vt100_blank(struct vt100 *v,struct vt100_cell *p) {
    *p=(struct vt100_cell){.ch=' ',.fg=(unsigned char)v->fg,
         .bg=(unsigned char)v->bg,.flags=0};
}
static void vt100_clear_rows(struct vt100 *v,unsigned first,unsigned last) {
    struct vt100_cell (*cells)[VT100_COLS]=vt100_active(v);
    for(unsigned row=first;row<=last && row<v->rows;row++)
        for(unsigned col=0;col<v->cols;col++)vt100_blank(v,&cells[row][col]);
}
void vt100_init(struct vt100 *v,unsigned cols,unsigned rows) {
    memset(v,0,sizeof(*v));
    v->cols=cols && cols<=VT100_COLS?cols:VT100_COLS;
    v->rows=rows && rows<=VT100_ROWS?rows:VT100_ROWS;
    v->fg=17;v->bg=16;v->wrap=1;v->show_cursor=1;
    v->scroll_bottom=v->rows-1;
    for(unsigned page=0;page<2;page++) {
        v->alternate=page;
        vt100_clear_rows(v,0,v->rows-1);
    }
    v->alternate=0;
}
static void vt100_scroll(struct vt100 *v,unsigned top,unsigned bottom,int up) {
    struct vt100_cell (*cells)[VT100_COLS]=vt100_active(v);
    if(bottom<=top || bottom>=v->rows)return;
    if(up) {
        memmove(cells[top],cells[top+1],
                (bottom-top)*sizeof(cells[0]));
        for(unsigned c=0;c<v->cols;c++)vt100_blank(v,&cells[bottom][c]);
    } else {
        memmove(cells[top+1],cells[top],
                (bottom-top)*sizeof(cells[0]));
        for(unsigned c=0;c<v->cols;c++)vt100_blank(v,&cells[top][c]);
    }
}
static void vt100_lf(struct vt100 *v) {
    v->wrapped=0;
    if(v->row==v->scroll_bottom)vt100_scroll(v,v->scroll_top,v->scroll_bottom,1);
    else if(v->row+1<v->rows)v->row++;
}
static void vt100_put(struct vt100 *v,unsigned char ch) {
    struct vt100_cell (*cells)[VT100_COLS]=vt100_active(v);
    if(v->wrapped) {v->col=0;vt100_lf(v);}
    if(v->graphics) {
        /* VT100 DEC Special Graphics; curses uses these for borders. */
        if(ch=='q')ch='-';
        else if(ch=='x')ch='|';
        else if(ch=='l'||ch=='k'||ch=='m'||ch=='j'||ch=='n'||
                ch=='t'||ch=='u'||ch=='v'||ch=='w')ch='+';
    }
    cells[v->row][v->col]=(struct vt100_cell){.ch=ch,
        .fg=(unsigned char)v->fg,.bg=(unsigned char)v->bg,
        .flags=(unsigned char)v->flags};
    if(v->col+1<v->cols)v->col++;
    else if(v->wrap)v->wrapped=1;
}
static unsigned vt100_arg(struct vt100 *v,unsigned i,unsigned def) {
    return i<v->parameter_count && v->params[i]?v->params[i]:def;
}
static unsigned vt100_min(unsigned n,unsigned maximum) {
    return n<maximum?n:maximum;
}
static void vt100_cursor(struct vt100 *v,unsigned row,unsigned col) {
    v->row=vt100_min(row,v->rows-1);
    v->col=vt100_min(col,v->cols-1);
    v->wrapped=0;
}
static void vt100_reply(struct vt100 *v,const char *str) {
    size_t len=strlen(str);
    if(len<=sizeof(v->reply)-v->reply_length) {
        memcpy(v->reply+v->reply_length,str,len);
        v->reply_length+=(unsigned)len;
    }
}
static void vt100_csi(struct vt100 *v,unsigned char action) {
    const unsigned n=vt100_arg(v,0,1);
    struct vt100_cell (*cells)[VT100_COLS]=vt100_active(v);
    if(v->private_mode) {
        for(unsigned i=0;i<v->parameter_count;i++) {
            unsigned p=v->params[i];
            if(action=='h'||action=='l') {
                unsigned enabled=action=='h';
                if(p==25)v->show_cursor=enabled;
                else if(p==1)v->application_cursor=enabled;
                else if(p==7)v->wrap=enabled;
                else if(p==6)v->origin_mode=enabled;
                else if(p==47||p==1047||p==1049) {
                    if(enabled && !v->alternate) {
                        v->saved_row=v->row;v->saved_col=v->col;
                        v->alternate=1;vt100_clear_rows(v,0,v->rows-1);
                        vt100_cursor(v,0,0);
                    } else if(!enabled && v->alternate) {
                        v->alternate=0;
                        vt100_cursor(v,v->saved_row,v->saved_col);
                    }
                }
            }
        }
        return;
    }
    switch(action) {
    case 'A':vt100_cursor(v,v->row>=n?v->row-n:0,v->col);break;
    case 'B':vt100_cursor(v,vt100_min(v->row+n,v->rows-1),v->col);break;
    case 'C':vt100_cursor(v,v->row,vt100_min(v->col+n,v->cols-1));break;
    case 'D':vt100_cursor(v,v->row,v->col>=n?v->col-n:0);break;
    case 'E':vt100_cursor(v,vt100_min(v->row+n,v->rows-1),0);break;
    case 'F':vt100_cursor(v,v->row>=n?v->row-n:0,0);break;
    case 'G':vt100_cursor(v,v->row,n-1);break;
    case 'd':vt100_cursor(v,n-1,v->col);break;
    case 'H':case 'f': {
        unsigned y=vt100_arg(v,0,1)-1;
        if(v->origin_mode)y+=v->scroll_top;
        vt100_cursor(v,y,vt100_arg(v,1,1)-1);
        break;
    }
    case 'J': {
        unsigned mode=vt100_arg(v,0,0);
        if(mode==2||mode==3)vt100_clear_rows(v,0,v->rows-1);
        else if(mode==0) {
            for(unsigned col=v->col;col<v->cols;col++)vt100_blank(v,&cells[v->row][col]);
            if(v->row+1<v->rows)vt100_clear_rows(v,v->row+1,v->rows-1);
        } else if(mode==1) {
            if(v->row)vt100_clear_rows(v,0,v->row-1);
            for(unsigned col=0;col<=v->col;col++)vt100_blank(v,&cells[v->row][col]);
        }
        break;
    }
    case 'K': {
        unsigned mode=vt100_arg(v,0,0);
        unsigned from=mode==0?v->col:0;
        unsigned to=mode==1?v->col:v->cols-1;
        for(unsigned col=from;col<=to;col++)vt100_blank(v,&cells[v->row][col]);
        break;
    }
    case 'L':case 'M':
        if(v->row>=v->scroll_top && v->row<=v->scroll_bottom) {
            unsigned count=vt100_min(n,v->scroll_bottom-v->row+1);
            for(unsigned i=0;i<count;i++)vt100_scroll(v,v->row,v->scroll_bottom,action=='M');
        }
        break;
    case '@':case 'P':case 'X': {
        unsigned count=vt100_min(n,v->cols-v->col);
        if(action=='@')memmove(&cells[v->row][v->col+count],
                     &cells[v->row][v->col],
                     (v->cols-v->col-count)*sizeof(cells[0][0]));
        if(action=='P')memmove(&cells[v->row][v->col],
                     &cells[v->row][v->col+count],
                     (v->cols-v->col-count)*sizeof(cells[0][0]));
        unsigned from=action=='P'?v->cols-count:v->col;
        for(unsigned i=0;i<count;i++)vt100_blank(v,&cells[v->row][from+i]);
        break;
    }
    case 'r': {
        unsigned top=vt100_arg(v,0,1)-1;
        unsigned bottom=vt100_arg(v,1,v->rows)-1;
        if(top<bottom && bottom<v->rows) {
            v->scroll_top=top;v->scroll_bottom=bottom;
            vt100_cursor(v,v->origin_mode?top:0,0);
        }
        break;
    }
    case 'm': {
        if(!v->parameter_count)v->parameter_count=1;
        for(unsigned i=0;i<v->parameter_count;i++) {
            unsigned p=v->params[i];
            if(p==0) {v->fg=17;v->bg=16;v->flags=0;}
            else if(p==1)v->flags|=VT_BOLD;
            else if(p==4)v->flags|=VT_UNDERLINE;
            else if(p==7)v->flags|=VT_REVERSE;
            else if(p==22)v->flags&=~VT_BOLD;
            else if(p==24)v->flags&=~VT_UNDERLINE;
            else if(p==27)v->flags&=~VT_REVERSE;
            else if(p>=30 && p<=37)v->fg=p-30;
            else if(p>=40 && p<=47)v->bg=p-40;
            else if(p>=90 && p<=97)v->fg=p-90+8;
            else if(p>=100 && p<=107)v->bg=p-100+8;
            else if(p==39)v->fg=17;
            else if(p==49)v->bg=16;
        }
        break;
    }
    case 's':v->saved_row=v->row;v->saved_col=v->col;break;
    case 'u':vt100_cursor(v,v->saved_row,v->saved_col);break;
    case 'n': {
        if(n==5)vt100_reply(v,"\033[0n");
        if(n==6) {
            char buffer[32];
            snprintf(buffer,sizeof(buffer),"\033[%u;%uR",v->row+1,v->col+1);
            vt100_reply(v,buffer);
        }
        break;
    }
    case 'c':vt100_reply(v,"\033[?1;2c");break;
    default:break;
    }
}
static void vt100_byte(struct vt100 *v,unsigned char c) {
    if(v->parse_state==VT_OSC) {
        if(c==7)v->parse_state=VT_NORMAL;
        else if(c==27)v->parse_state=VT_OSC_ESC;
        return;
    }
    if(v->parse_state==VT_OSC_ESC) {
        v->parse_state=c=='\\'?VT_NORMAL:VT_OSC;
        return;
    }
    if(v->parse_state==VT_IGNORE) {
        if(c==27)v->parse_state=VT_IGNORE_ESC;
        return;
    }
    if(v->parse_state==VT_IGNORE_ESC) {
        v->parse_state=c=='\\'?VT_NORMAL:VT_IGNORE;
        return;
    }
    if(v->parse_state==VT_CHARSET) {
        if(v->charset_select)v->g1_graphics=c=='0';
        else v->g0_graphics=c=='0';
        v->parse_state=VT_NORMAL;return;
    }
    if(v->parse_state==VT_CSI) {
        if(c=='?' && !v->parameter_count && !v->parameter) {
            v->private_mode=1;return;
        }
        if(c>='0' && c<='9') {
            if(v->parameter<12) {
                v->params[v->parameter]=vt100_min(
                    v->params[v->parameter]*10U+(unsigned)(c-'0'),65535);
                if(v->parameter_count<v->parameter+1)
                    v->parameter_count=v->parameter+1;
            }
            return;
        }
        if(c==';') {
            if(v->parameter<11)v->parameter++;
            v->parameter_count=v->parameter+1;return;
        }
        if(c>=0x40 && c<=0x7e) {
            vt100_csi(v,c);v->parse_state=VT_NORMAL;return;
        }
        if(c==27)v->parse_state=VT_ESC;
        else if(c<0x20 || c>0x3f)v->parse_state=VT_NORMAL;
        return;
    }
    if(v->parse_state==VT_ESC) {
        v->parse_state=VT_NORMAL;
        switch(c) {
        case '[':v->parse_state=VT_CSI;v->private_mode=0;
                 v->parameter_count=0;v->parameter=0;
                 memset(v->params,0,sizeof(v->params));return;
        case ']':v->parse_state=VT_OSC;return;
        case 'P':case '^':case '_':v->parse_state=VT_IGNORE;return;
        case '(':case ')':v->charset_select=c==')';
                 v->parse_state=VT_CHARSET;return;
        case '7':v->saved_row=v->row;v->saved_col=v->col;return;
        case '8':vt100_cursor(v,v->saved_row,v->saved_col);return;
        case 'D':vt100_lf(v);return;
        case 'E':v->col=0;vt100_lf(v);return;
        case 'M':if(v->row==v->scroll_top)
                    vt100_scroll(v,v->scroll_top,v->scroll_bottom,0);
                 else if(v->row)v->row--;
                 return;
        case 'c':vt100_init(v,v->cols,v->rows);return;
        case 'Z':vt100_reply(v,"\033[?1;2c");return;
        default:return;
        }
    }
    if(c==27) {v->parse_state=VT_ESC;v->utf8_remaining=0;return;}
    if(c==14) {v->graphics=v->g1_graphics;return;} /* SO: invoke G1 */
    if(c==15) {v->graphics=v->g0_graphics;return;} /* SI: invoke G0 */
    if(c=='\r') {v->col=0;v->wrapped=0;return;}
    if(c=='\n'||c=='\v'||c=='\f') {vt100_lf(v);return;}
    if(c=='\b') {if(v->col)v->col--;v->wrapped=0;return;}
    if(c=='\t') {unsigned col=(v->col+8)&~7U;
                  vt100_cursor(v,v->row,vt100_min(col,v->cols-1));return;}
    if(c<0x20 || c==0x7f)return;
    if(c>=0x80) {
        if((c&0xc0)==0xc0) {
            v->utf8_remaining=(c&0xe0)==0xc0?1:
                 (c&0xf0)==0xe0?2:(c&0xf8)==0xf0?3:0;
            vt100_put(v,'?');
        } else if((c&0xc0)==0x80 && v->utf8_remaining)
            v->utf8_remaining--;
        return;
    }
    v->utf8_remaining=0;
    vt100_put(v,c);
}
void vt100_feed(struct vt100 *v,const char *bytes,size_t length) {
    for(size_t i=0;i<length;i++)vt100_byte(v,(unsigned char)bytes[i]);
}
size_t vt100_take_reply(struct vt100 *v,char *dst,size_t capacity) {
    size_t n=v->reply_length;
    if(n>capacity)return 0;
    memcpy(dst,v->reply,n);v->reply_length=0;
    return n;
}
const struct vt100_cell *vt100_cell_at(const struct vt100 *v,unsigned row,unsigned col) {
    if(row>=v->rows || col>=v->cols)return NULL;
    return &v->pages[v->alternate][row][col];
}
#endif
