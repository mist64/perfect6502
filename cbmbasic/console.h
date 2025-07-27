#ifndef CONSOLE_H_INCLUDED
#define CONSOLE_H_INCLUDED

enum {
    COLOR_WHITE,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_BLACK,
    COLOR_PURPLE,
    COLOR_YELLOW,
    COLOR_CYAN,
    COLOR_ORANGE,
    COLOR_BROWN,
    COLOR_LTRED,
    COLOR_GREY1,
    COLOR_GREY2,
    COLOR_LTGREEN,
    COLOR_LTBLUE,
    COLOR_GREY3,
};

void clear_screen(void);
void up_cursor(void);
void down_cursor(void);
void left_cursor(void);
void right_cursor(void);
void move_cursor(int x, int y);
void get_cursor(int* x, int* y);
void set_color(int c);

#endif /* CONSOLE_H_INCLUDED */
