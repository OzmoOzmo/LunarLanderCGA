#include <conio.h>
#include <dos.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 320
#define HEIGHT 200
#define ROTATION_STEP 1
#define REPEAT_ACCEL_START 3
#define TICK_PERIOD 23864UL
#define PIVOT_X 160
#define PIVOT_Y 94
#define MAX_PATCH 96
#define ROTATION_SCALE 1024
#define STARTING_FUEL 1000
#define THRUST_FUEL_RATE 2
#define GRAVITY_ACCEL 1
#define THRUST_ACCEL 2
#define TERRAIN_COLUMNS 32
#define PAD_LEFT 120
#define PAD_RIGHT 200

typedef struct {
    unsigned char width, height, pivot_x, pivot_y;
    unsigned char far *pixels;
} Frame;

typedef struct {
    unsigned short width, height;
    unsigned char far *pixels;
} Sprite;

static Frame frames[28];
static Frame on_frames[28];
static Frame source_frame;
static Sprite moon;
static short sine_table[360];
static short cosine_table[360];
static unsigned short terrain[TERRAIN_COLUMNS];
static int ship_x = 160, ship_y = 80, vel_x = 0, vel_y = 0, fuel = STARTING_FUEL;
static unsigned short angle = 0;
static int game_over = 0, success = 0;
static unsigned char patch[MAX_PATCH * MAX_PATCH];
static volatile unsigned char keys[128];
static void (__interrupt __far *previous_keyboard)(void);
static unsigned short vbe_segment, active_bank;
static unsigned long vbe_granularity;
static unsigned char mode;
static unsigned char mode_error;

static unsigned char rgb(unsigned char red, unsigned char green,
                         unsigned char blue)
{
    return (unsigned char)(1 + red * 36 + green * 6 + blue);
}

static void set_video(unsigned short number)
{
    union REGS regs;
    regs.w.ax = number;
    int86(0x10, &regs, &regs);
}

static int set_mode(unsigned char selected)
{
    union REGS regs;
    struct SREGS segments;
    static unsigned char vbe_info[512];
    static unsigned char mode_info[256];
    unsigned short old_mode;

    mode = selected;
    mode_error = 0;
    if (mode == 0) {
        set_video(4);
        regs.h.ah = 15;
        int86(0x10, &regs, &regs);
        return regs.h.al == 4;
    }
    if (mode == 1) {
        set_video(0x13);
        regs.h.ah = 15;
        int86(0x10, &regs, &regs);
        return regs.h.al == 0x13;
    }

    memset(vbe_info, 0, sizeof(vbe_info));
    memcpy(vbe_info, "VBE2", 4);
    regs.w.ax = 0x4f00;
    segments.es = FP_SEG(vbe_info);
    regs.x.di = FP_OFF(vbe_info);
    int86x(0x10, &regs, &regs, &segments);
    if (regs.w.ax != 0x004f) { mode_error = 1; return 0; }

    memset(mode_info, 0, sizeof(mode_info));
    regs.w.ax = 0x4f01;
    regs.w.cx = 0x0103;
    segments.es = FP_SEG(mode_info);
    regs.x.di = FP_OFF(mode_info);
    int86x(0x10, &regs, &regs, &segments);
    if (regs.w.ax != 0x004f) { mode_error = 2; return 0; }
    if (!(mode_info[0] & 1) || !(mode_info[2] & 4) ||
        mode_info[25] != 8) { mode_error = 3; return 0; }
    if (mode_info[16] != 32 || mode_info[17] != 3 ||
        mode_info[18] != 32 || mode_info[19] != 3 ||
        mode_info[20] != 88 || mode_info[21] != 2)
        { mode_error = 4; return 0; }
    if (!mode_info[4] || !(mode_info[8] | mode_info[9]))
        { mode_error = 5; return 0; }
    vbe_granularity = (unsigned long)(mode_info[4] | (mode_info[5] << 8)) * 1024UL;
    vbe_segment = (unsigned short)(mode_info[8] | (mode_info[9] << 8));
    if (vbe_granularity != 16384U && vbe_granularity != 65536UL)
        { mode_error = 6; return 0; }
    regs.w.ax = 0x4f02;
    regs.w.bx = 0x0103;
    int86(0x10, &regs, &regs);
    if (regs.w.ax != 0x004f) { mode_error = 7; return 0; }
    old_mode = 0xffff;
    regs.w.ax = 0x4f03;
    int86(0x10, &regs, &regs);
    if (regs.w.ax == 0x004f) old_mode = regs.w.bx & 0x3fff;
    active_bank = 0xffff;
    if (old_mode != 0x0103) mode_error = 8;
    return old_mode == 0x0103;
}

static void set_palette(void)
{
    unsigned short index;
    unsigned char value;
    if (mode == 0) {
        outp(0x3d9, 0x30);
        return;
    }
    outp(0x3c8, 0);
    for (index = 0; index < 256; ++index) {
        value = index ? (unsigned char)(index - 1) : 0;
        outp(0x3c9, index ? ((value / 36) * 63 / 5) : 0);
        outp(0x3c9, index ? (((value / 6) % 6) * 63 / 5) : 0);
        outp(0x3c9, index ? ((value % 6) * 63 / 5) : 0);
    }
}

static unsigned char cga_color(unsigned char color)
{
    unsigned char value, red, green, blue;
    unsigned short cyan, magenta, white;
    if (!color) return 0;
    value = color - 1;
    red = value / 36;
    green = (value / 6) % 6;
    blue = value % 6;
    cyan = red * red + (5 - green) * (5 - green) + (5 - blue) * (5 - blue);
    magenta = (5 - red) * (5 - red) + green * green + (5 - blue) * (5 - blue);
    white = (5 - red) * (5 - red) + (5 - green) * (5 - green) +
            (5 - blue) * (5 - blue);
    if (red + green + blue < 5) return 0;
    if (cyan <= magenta && cyan <= white) return 1;
    if (magenta <= white) return 2;
    return 3;
}

static void bank(unsigned short number)
{
    union REGS regs;
    if (active_bank == number) return;
    regs.w.ax = 0x4f05;
    regs.w.bx = 0;
    regs.w.dx = number;
    int86(0x10, &regs, &regs);
    active_bank = number;
}

static void put_pixel(unsigned short x, unsigned short y, unsigned char color)
{
    unsigned char far *screen;
    unsigned long offset;
    unsigned short location, shift;
    if (mode == 0) {
        screen = (unsigned char far *)MK_FP(0xb800, 0);
        location = (unsigned short)((y & 1) * 0x2000 +
                    (y >> 1) * 80 + (x >> 2));
        shift = (unsigned short)((3 - (x & 3)) * 2);
        screen[location] = (unsigned char)((screen[location] & ~(3 << shift)) |
                            (cga_color(color) << shift));
    } else if (mode == 1) {
        screen = (unsigned char far *)MK_FP(0xa000, 0);
        screen[y * 320U + x] = color;
    } else {
        screen = (unsigned char far *)MK_FP(vbe_segment, 0);
        offset = (unsigned long)y * 800UL + x;
        bank((unsigned short)((offset / 65536UL) *
                            (65536UL / vbe_granularity)));
        screen[(unsigned short)offset] = color;
    }
}

static void plot(unsigned short x, unsigned short y, unsigned char color)
{
    if (mode == 2) {
        y = (unsigned short)(y * 2 + 100);
        x = (unsigned short)(x * 2 + 80);
        put_pixel(x, y, color);
        put_pixel(x + 1, y, color);
        put_pixel(x, y + 1, color);
        put_pixel(x + 1, y + 1, color);
    } else {
        put_pixel(x, y, color);
    }
}

static void text(unsigned char row, unsigned char column, const char *message)
{
    union REGS regs;
    regs.h.ah = 2;
    regs.h.bh = 0;
    regs.h.dh = row;
    regs.h.dl = column;
    int86(0x10, &regs, &regs);
    while (*message) {
        regs.h.ah = 14;
        regs.h.al = *message++;
        regs.h.bh = 0;
        regs.h.bl = mode == 0 ? 3 : rgb(5, 5, 5);
        int86(0x10, &regs, &regs);
    }
}

static unsigned char background(int x, int y)
{
    unsigned long star;
    long dx = x - 259, dy = y - 62;
    long radius = dx * dx + dy * dy;
    if (radius < 196) return rgb(4, 4, 3);
    if (radius < 256) return rgb(5, 5, 5);
    if (y < 30 || y >= 172) return 0;
    star = ((unsigned long)x * 19937UL + (unsigned long)y * 7919UL);
    star ^= (star >> 7);
    if (star % 223UL == 0) return rgb(5, 5, 5);
    if (star % 127UL == 0) return rgb(2, 3, 4);
    return 0;
}

static void build_rotation_tables(void)
{
    int index;
    double angle;
    for (index = 0; index < 360; ++index) {
        angle = (double)index * 3.14159265358979323846 / 180.0;
        sine_table[index] = (short)(sin(angle) * ROTATION_SCALE + 0.5);
        cosine_table[index] = (short)(cos(angle) * ROTATION_SCALE + 0.5);
    }
}

static void restore_background_region(int left, int top, int right, int bottom)
{
    int x, y;
    if (left < 0) left = 0;
    if (top < 30) top = 30;
    if (right > WIDTH) right = WIDTH;
    if (bottom > 172) bottom = 172;
    for (y = top; y < bottom; ++y) {
        for (x = left; x < right; ++x)
            plot(x, y, background(x, y));
    }
}

static void render_rotated_sprite(unsigned short angle, int origin_x, int origin_y,
                                 int *left, int *top, int *right, int *bottom)
{
    int x, y, min_x = 32767, min_y = 32767, max_x = -32767, max_y = -32767;
    int local_x, local_y, screen_x, screen_y, src_x, src_y;
    int cosv = cosine_table[angle];
    int sinv = sine_table[angle];
    unsigned char color;
    unsigned short offset;
    for (y = 0; y < source_frame.height; ++y) {
        for (x = 0; x < source_frame.width; ++x) {
            if (!source_frame.pixels[(unsigned short)(y * source_frame.width + x)])
                continue;
            local_x = x - source_frame.pivot_x;
            local_y = y - source_frame.pivot_y;
            screen_x = ((local_x * cosv - local_y * sinv) >> 10) + origin_x;
            screen_y = ((local_x * sinv + local_y * cosv) >> 10) + origin_y;
            if (screen_x < min_x) min_x = screen_x;
            if (screen_y < min_y) min_y = screen_y;
            if (screen_x > max_x) max_x = screen_x;
            if (screen_y > max_y) max_y = screen_y;
        }
    }
    if (min_x == 32767) {
        *left = origin_x - 1;
        *top = origin_y - 1;
        *right = origin_x + 1;
        *bottom = origin_y + 1;
        return;
    }
    if (min_x < 0) min_x = 0;
    if (min_y < 30) min_y = 30;
    if (max_x >= WIDTH) max_x = WIDTH - 1;
    if (max_y > 172) max_y = 172;
    *left = min_x;
    *top = min_y;
    *right = max_x + 1;
    *bottom = max_y + 1;
    for (y = min_y; y < *bottom; ++y) {
        for (x = min_x; x < *right; ++x) {
            color = background(x, y);
            local_x = x - origin_x;
            local_y = y - origin_y;
            src_x = source_frame.pivot_x + ((local_x * cosv + local_y * sinv) >> 10);
            src_y = source_frame.pivot_y + ((-local_x * sinv + local_y * cosv) >> 10);
            if (src_x >= 0 && src_x < source_frame.width &&
                src_y >= 0 && src_y < source_frame.height) {
                offset = (unsigned short)(src_y * source_frame.width + src_x);
                if (source_frame.pixels[offset]) color = source_frame.pixels[offset];
            }
            patch[(y - min_y) * MAX_PATCH + (x - min_x)] = color;
        }
    }
}

static void draw_moon(void)
{
    unsigned short x, y;
    unsigned short offset;
    for (y = 0; y < moon.height; ++y) {
        for (x = 0; x < moon.width; ++x) {
            offset = (unsigned short)(y * moon.width + x);
            if (moon.pixels[offset])
                plot((unsigned short)(x + 180), (unsigned short)(y + 35),
                     moon.pixels[offset]);
        }
    }
}

static void generate_terrain(void)
{
    unsigned short profile[TERRAIN_COLUMNS] = {
        176, 170, 160, 150, 158, 170, 176, 168, 160, 150, 148, 160, 170,
        176, 170, 160, 152, 146, 150, 160, 168, 172, 166, 156, 148, 152,
        162, 170, 176, 170, 160, 150
    };
    unsigned short i;
    for (i = 0; i < TERRAIN_COLUMNS; ++i) {
        terrain[i] = profile[i];
    }
    for (i = PAD_LEFT / 10; i <= PAD_RIGHT / 10; ++i)
        terrain[i] = 150;
}

static unsigned short terrain_height_at(int x)
{
    int column, offset;
    unsigned short left, right;
    if (x < 0) x = 0;
    if (x >= WIDTH) x = WIDTH - 1;
    column = x / 10;
    if (column < 0) column = 0;
    if (column >= TERRAIN_COLUMNS - 1) column = TERRAIN_COLUMNS - 2;
    offset = x % 10;
    left = terrain[column];
    right = terrain[column + 1];
    return (unsigned short)(left + ((right - left) * offset) / 10);
}

static void draw_terrain(void)
{
    unsigned short x, y, ground;
    for (x = 0; x < WIDTH; ++x) {
        ground = terrain_height_at((int)x);
        for (y = ground; y < HEIGHT; ++y)
            plot(x, y, rgb(5, 5, 5));
    }
}

static void draw_result_graphic(void)
{
    unsigned short x, y;
    if (success) {
        for (y = 0; y < 8; ++y)
            plot((unsigned short)(PAD_LEFT + 16), (unsigned short)(terrain_height_at(PAD_LEFT + 16) - 8 + y), rgb(5, 5, 5));
        for (x = 0; x < 18; ++x)
            plot((unsigned short)(PAD_LEFT + 18 + x), (unsigned short)(terrain_height_at(PAD_LEFT + 16) - 8), rgb(5, 5, 5));
        for (x = 0; x < 18; ++x)
            plot((unsigned short)(PAD_LEFT + 18 + x), (unsigned short)(terrain_height_at(PAD_LEFT + 16) - 6), rgb(2, 3, 4));
        text(6, 10, "SUCCESS");
    } else {
        for (y = 0; y < 10; ++y)
            plot((unsigned short)(ship_x - 2 + y / 3), (unsigned short)(ship_y + y), rgb(5, 5, 5));
        for (x = 0; x < 8; ++x)
            plot((unsigned short)(ship_x + x), (unsigned short)(ship_y + 4), rgb(5, 5, 5));
        text(6, 10, "CRASH");
    }
    text(8, 10, "SPACE TO RETRY");
}

static void scene(void)
{
    unsigned short x, y;
    unsigned char color;
    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            color = background(x, y);
            if (color) plot(x, y, color);
        }
    }
    draw_moon();
    draw_terrain();
    if (mode == 2) {
        text(1, 1, "MOONLANDER");
        text(2, 1, "DOS / PHASE 3");
        text(33, 1, "Z LEFT  X RIGHT  SPACE THRUST  Q MENU");
    } else {
        text(0, 1, "MOONLANDER");
        text(1, 1, "DOS / PHASE 3");
        text(23, 1, "Z LEFT  X RIGHT  SPACE THRUST  Q MENU");
    }
}

static void free_frames(void)
{
    unsigned short index;
    for (index = 0; index < 28; ++index) {
        free(frames[index].pixels);
        frames[index].pixels = NULL;
        free(on_frames[index].pixels);
        on_frames[index].pixels = NULL;
    }
    free(moon.pixels);
    moon.pixels = NULL;
    source_frame.pixels = NULL;
}

static int load_moon(void)
{
    FILE *input;
    unsigned char header[4];
    unsigned short width, height, offset, x, y;
    input = fopen("MOON.DAT", "rb");
    if (!input) return 0;
    if (fread(header, 1, 4, input) != 4 || memcmp(header, "MNS1", 4)) goto bad;
    if (fread(&width, 1, 2, input) != 2 || fread(&height, 1, 2, input) != 2)
        goto bad;
    moon.width = width;
    moon.height = height;
    moon.pixels = malloc((unsigned)(width * height));
    if (!moon.pixels) goto bad;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            offset = (unsigned short)(y * width + x);
            if (fread(&moon.pixels[offset], 1, 1, input) != 1) goto bad;
        }
    }
    fclose(input);
    return 1;
bad:
    fclose(input);
    free(moon.pixels);
    moon.pixels = NULL;
    return 0;
}

static int load_frames(void)
{
    FILE *input, *input_on;
    unsigned char header[5], shape[4];
    unsigned short index, size;
    input = fopen("LANDOFF.DAT", "rb");
    input_on = fopen("LANDON.DAT", "rb");
    if (!input || !input_on) { if (input) fclose(input); if (input_on) fclose(input_on); return 0; }
    if (fread(header, 1, 5, input) != 5 ||
        memcmp(header, "LLS1", 4) || header[4] != 28) goto bad;
    for (index = 0; index < 28; ++index) {
        if (fread(shape, 1, 4, input) != 4) goto bad;
        frames[index].width = shape[0];
        frames[index].height = shape[1];
        frames[index].pivot_x = shape[2];
        frames[index].pivot_y = shape[3];
        if (!shape[0] || !shape[1] || shape[0] > 48 || shape[1] > 48 ||
            shape[2] >= shape[0] || shape[3] >= shape[1]) goto bad;
        size = (unsigned short)shape[0] * shape[1];
        frames[index].pixels = malloc(size);
        if (!frames[index].pixels ||
            fread(frames[index].pixels, 1, size, input) != size) goto bad;
    }
    if (fgetc(input) != EOF) goto bad;
    fclose(input);
    if (fread(header, 1, 5, input_on) != 5 ||
        memcmp(header, "LLS1", 4) || header[4] != 28) goto bad;
    for (index = 0; index < 28; ++index) {
        if (fread(shape, 1, 4, input_on) != 4) goto bad;
        on_frames[index].width = shape[0];
        on_frames[index].height = shape[1];
        on_frames[index].pivot_x = shape[2];
        on_frames[index].pivot_y = shape[3];
        size = (unsigned short)shape[0] * shape[1];
        on_frames[index].pixels = malloc(size);
        if (!on_frames[index].pixels ||
            fread(on_frames[index].pixels, 1, size, input_on) != size) goto bad;
    }
    if (fgetc(input_on) != EOF) goto bad;
    fclose(input_on);
    source_frame = frames[0];
    return load_moon();
bad:
    if (input) fclose(input);
    if (input_on) fclose(input_on);
    free_frames();
    return 0;
}

static unsigned char frame_number(unsigned short angle)
{
    unsigned short quadrant = angle / 90;
    unsigned short acute = angle % 90;
    if (quadrant & 1) acute = 90 - acute;
    return (unsigned char)(quadrant * 7 + (acute + 7) / 15);
}

static void render(unsigned char previous, unsigned char current)
{
    Frame *old_frame = &frames[previous == 255 ? current : previous];
    Frame *new_frame = &frames[current];
    int left, top, right, bottom, x, y, sx, sy;
    unsigned char color;
    unsigned short offset;
    if (previous == current) return;
    left = PIVOT_X - old_frame->pivot_x;
    top = PIVOT_Y - old_frame->pivot_y;
    right = left + old_frame->width;
    bottom = top + old_frame->height;
    x = PIVOT_X - new_frame->pivot_x;
    y = PIVOT_Y - new_frame->pivot_y;
    if (x < left) left = x;
    if (y < top) top = y;
    if (x + new_frame->width > right) right = x + new_frame->width;
    if (y + new_frame->height > bottom) bottom = y + new_frame->height;
    if (left < 0) left = 0;
    if (top < 30) top = 30;
    if (right > WIDTH) right = WIDTH;
    if (bottom > 172) bottom = 172;
    for (y = top; y < bottom; ++y) {
        for (x = left; x < right; ++x) {
            color = background(x, y);
            sx = x - (PIVOT_X - new_frame->pivot_x);
            sy = y - (PIVOT_Y - new_frame->pivot_y);
            if (sx >= 0 && sx < new_frame->width &&
                sy >= 0 && sy < new_frame->height) {
                offset = (unsigned short)(sy * new_frame->width + sx);
                if (new_frame->pixels[offset]) color = new_frame->pixels[offset];
            }
            patch[(y - top) * MAX_PATCH + (x - left)] = color;
        }
    }
    for (y = top; y < bottom; ++y) {
        for (x = left; x < right; ++x)
            plot(x, y, patch[(y - top) * MAX_PATCH + (x - left)]);
    }
}

static void __interrupt __far keyboard_interrupt(void)
{
    unsigned char scan = inp(0x60);
    unsigned char status = inp(0x61);
    outp(0x61, status | 0x80);
    outp(0x61, status);
    if ((scan & 0x7f) < 128) keys[scan & 0x7f] = !(scan & 0x80);
    outp(0x20, 0x20);
}

static unsigned long timer(void)
{
    union REGS regs;
    unsigned short low, high;
    regs.h.ah = 0;
    int86(0x1a, &regs, &regs);
    outp(0x43, 0);
    low = inp(0x40);
    high = inp(0x40);
    return (((unsigned long)regs.w.cx << 16) | regs.w.dx) * 65536UL +
           (65535U - ((high << 8) | low));
}

static void wait_retrace(void)
{
    unsigned short timeout = 65000;
    while ((inp(0x3da) & 8) && --timeout) {}
    timeout = 65000;
    while (!(inp(0x3da) & 8) && --timeout) {}
}

static void play(void)
{
    unsigned short last_angle = 65535U;
    unsigned char old_q = 0, old_space = 0;
    unsigned long next_tick;
    int left = 0, top = 0, right = 0, bottom = 0;
    int old_left = -1, old_top = -1, old_right = -1, old_bottom = -1;
    int last_ship_x, last_ship_y;
    int thrusting = 0;
    int x, y, dx, dy;
    unsigned short ground;
    generate_terrain();
    ship_x = 160;
    ship_y = 60;
    vel_x = 0;
    vel_y = 0;
    fuel = STARTING_FUEL;
    angle = 0;
    game_over = 0;
    success = 0;
    memset((void *)keys, 0, sizeof(keys));
    scene();
    source_frame = frames[0];
    render_rotated_sprite(angle, ship_x, ship_y, &left, &top, &right, &bottom);
    for (y = top; y < bottom; ++y) {
        for (x = left; x < right; ++x)
            plot(x, y, patch[(y - top) * MAX_PATCH + (x - left)]);
    }
    old_left = left;
    old_top = top;
    old_right = right;
    old_bottom = bottom;
    last_ship_x = ship_x;
    last_ship_y = ship_y;
    last_angle = angle;
    previous_keyboard = _dos_getvect(9);
    _dos_setvect(9, keyboard_interrupt);
    next_tick = timer();
    for (;;) {
        if ((long)(timer() - next_tick) < 0) continue;
        next_tick += TICK_PERIOD;
        if ((long)(timer() - next_tick) > (long)(TICK_PERIOD * 3UL))
            next_tick = timer();
        if (keys[0x10] && !old_q) break;
        old_q = keys[0x10];

        if (!game_over) {
            if (keys[0x2c] && !keys[0x2d])
                angle = (unsigned short)((angle + 360U - 5U) % 360U);
            if (keys[0x2d] && !keys[0x2c])
                angle = (unsigned short)((angle + 5U) % 360U);

            thrusting = keys[0x39] && fuel > 0;
            if (thrusting) {
                fuel -= THRUST_FUEL_RATE;
                if (fuel < 0) fuel = 0;
                dx = (int)(sine_table[angle] * THRUST_ACCEL / ROTATION_SCALE);
                dy = (int)(-cosine_table[angle] * THRUST_ACCEL / ROTATION_SCALE);
                vel_x += dx;
                vel_y += dy;
            }

            vel_y += GRAVITY_ACCEL;
            ship_x += vel_x;
            ship_y += vel_y;

            if (ship_x < 20) { ship_x = 20; vel_x = 0; }
            if (ship_x > WIDTH - 20) { ship_x = WIDTH - 20; vel_x = 0; }
            if (ship_y < 36) { ship_y = 36; vel_y = 0; }

            ground = terrain_height_at(ship_x);
            if (ship_y + 12 >= (int)ground) {
                if (ship_x >= PAD_LEFT && ship_x <= PAD_RIGHT &&
                    (angle <= 10U || angle >= 350U) &&
                    abs(vel_x) <= 2 && abs(vel_y) <= 2) {
                    success = 1;
                    ship_y = (int)ground - 12;
                    vel_x = 0;
                    vel_y = 0;
                } else {
                    success = 0;
                    ship_y = (int)ground - 10;
                    vel_x = 0;
                    vel_y = 0;
                }
                game_over = 1;
            }

            if (thrusting)
                source_frame = on_frames[0];
            else
                source_frame = frames[0];
        }

        if (game_over) {
            draw_result_graphic();
            if (keys[0x39] && !old_space) {
                generate_terrain();
                ship_x = 160;
                ship_y = 60;
                vel_x = 0;
                vel_y = 0;
                fuel = STARTING_FUEL;
                angle = 0;
                game_over = 0;
                success = 0;
                scene();
                old_space = 1;
                source_frame = frames[0];
            }
            old_space = keys[0x39];
            continue;
        }

        if (angle == last_angle && ship_x == last_ship_x && ship_y == last_ship_y)
            continue;
        if (old_left >= 0 && old_top >= 0 && old_right >= 0 && old_bottom >= 0)
            restore_background_region(old_left, old_top, old_right, old_bottom);
        wait_retrace();
        render_rotated_sprite(angle, ship_x, ship_y, &left, &top, &right, &bottom);
        for (y = top; y < bottom; ++y) {
            for (x = left; x < right; ++x)
                plot(x, y, patch[(y - top) * MAX_PATCH + (x - left)]);
        }
        old_left = left;
        old_top = top;
        old_right = right;
        old_bottom = bottom;
        last_ship_x = ship_x;
        last_ship_y = ship_y;
        last_angle = angle;
    }
    _dos_setvect(9, previous_keyboard);
    while (kbhit()) getch();
}

int main(void)
{
    unsigned char choice;
    union REGS regs;
    unsigned short original;
    regs.h.ah = 15;
    int86(0x10, &regs, &regs);
    original = regs.h.al;
    build_rotation_tables();
    if (!load_frames()) {
        puts("Cannot read LANDOFF.DAT. Run convert_sprites.py first.");
        return 1;
    }
    for (;;) {
        set_video(3);
        puts("\n  MOONLANDER / DOS / PHASE 3\n");
        puts("  1  CGA   320x200 / 4 colors");
        puts("  2  VGA   320x200 / 256 colors");
        puts("  3  SVGA  800x600 / VBE 103h");
        puts("\n  Z / X rotate  -  SPACE thrust  -  Q returns to menu");
        puts("  Press 1, 2, 3 or ESC to exit.");
        if (mode_error) printf("  Previous mode failure: check %u\n", mode_error);
        choice = getch();
        if (choice == 27) break;
        if (choice < '1' || choice > '3') continue;
        if (!set_mode(choice - '1')) {
            set_video(3);
                 printf("Mode unavailable (check %u). Check DOSBox configuration.\n",
                     mode_error);
            getch();
            continue;
        }
        set_palette();
        play();
    }
    set_video(original);
    free_frames();
    return 0;
}
