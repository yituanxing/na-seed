#define NASD_VT100_IMPLEMENTATION
#include "../user/desktop/vt100.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void feed(struct vt100 *v,const char *data) {
    vt100_feed(v,data,strlen(data));
}
static unsigned char cell(struct vt100 *v,unsigned row,unsigned col) {
    const struct vt100_cell *c=vt100_cell_at(v,row,col);
    assert(c!=NULL);return c->ch;
}
int main(void) {
    struct vt100 v;
    vt100_init(&v,12,6);
    feed(&v,"abc\r\nDEF\033[1;5Hxy\033[");
    assert(cell(&v,0,0)=='a' && cell(&v,1,0)=='D');
    assert(cell(&v,0,4)=='x' && cell(&v,0,5)=='y');
    /* Partial CSI and strict real positions in a bounded VT viewport. */
    feed(&v,"2;3HZ\033[0K");
    assert(cell(&v,1,2)=='Z' && cell(&v,1,3)==' ');
    feed(&v,"\033[2;1H\033[31;44;1mR\033[0m");
    assert(cell(&v,1,0)=='R');
    assert(v.pages[0][1][0].fg==1 && v.pages[0][1][0].bg==4);
    feed(&v,"\033[2;1H\033[2P");
    assert(cell(&v,1,0)=='Z');
    feed(&v,"\033[2;4r\033[4;1H\n");
    assert(v.row==3 && v.scroll_top==1 && v.scroll_bottom==3);
    feed(&v,"\033[6;12H@\033[6n");
    char response[128]={0};
    size_t n=vt100_take_reply(&v,response,sizeof(response));
    assert(n==7 && !memcmp(response,"\033[6;12R",n));
    feed(&v,"\033[?1049hALT\033[?25l");
    assert(v.alternate && !v.show_cursor && cell(&v,0,0)=='A');
    feed(&v,"\033[?1049l");
    assert(!v.alternate && cell(&v,0,0)=='a');
    feed(&v,"\033]0;do not draw\007\033[H!");
    assert(cell(&v,0,0)=='!');
    feed(&v,"\033)0\016lqqk\017");
    assert(cell(&v,0,1)=='+' && cell(&v,0,2)=='-');
    feed(&v,"\033[2J");
    for(unsigned row=0;row<v.rows;row++)
        for(unsigned col=0;col<v.cols;col++)assert(cell(&v,row,col)==' ');
    vt100_init(&v,3,2);
    feed(&v,"abcdefg");
    assert(cell(&v,0,0)=='d' && cell(&v,1,0)=='g');
    puts("[M187 HOST VT100 PASS] CSI cursor/erase/colors/scroll/insert/delete, "
         "alternate screen, DEC graphics, query reply, chunked stream");
    return 0;
}
