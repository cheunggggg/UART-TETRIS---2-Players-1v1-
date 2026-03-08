#include <stdio.h>
#include <stdlib.h>
#include "platform.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xil_io.h"
#include "sleep.h"
#include "PmodBT2.h"
#include "PmodKYPD.h"
#include "xuartns550_l.h"

#define WIDTH 10
#define HEIGHT 20

#define GRAVITY_TICKS     1
#define FRAME_DELAY_START 400   // starting frame delay
#define FRAME_DELAY_MIN   80    // fastest the game can get
#define SPEED_INTERVAL    30    // decrease delay every ~30 seconds

#define DEFAULT_KEYTABLE "0FED789C456B123A"

// Wired keyboard - AXI UARTLite (PuTTY)
#define UART_P1_BASEADDR        XPAR_AXI_UARTLITE_0_BASEADDR
#define UART_RX_FIFO            0x00
#define UART_TX_FIFO            0x04
#define UART_STAT_REG           0x08
#define UART_STAT_RX_VALID      0x01
#define UART_STAT_TX_FULL       0x08

// BT2
#define BT2_UART_BASEADDR       XPAR_PMODBT2_0_AXI_LITE_UART_BASEADDR
#define BT2_GPIO_BASEADDR       XPAR_PMODBT2_0_AXI_LITE_GPIO_BASEADDR
#define BT2_BAUD                9600

// KYPD
#define KYPD_GPIO_BASEADDR      XPAR_PMODKYPD_0_AXI_LITE_GPIO_BASEADDR

// Basys3 Push Buttons - AXI GPIO channel 1
#define BTN_BASEADDR            XPAR_AXI_GPIO_0_BASEADDR
#define BTN_DATA_REG            0x00
#define BTN_UP                  0x01  // bit 0 = BTNU = rotate
#define BTN_LEFT                0x02  // bit 1 = BTNL = move left
#define BTN_RIGHT               0x04  // bit 2 = BTNR = move right
#define BTN_DOWN                0x08  // bit 3 = BTND = hard drop
// BTNC = CPU reset, not usable as GPIO

// Basys3 DIP Switches - AXI GPIO channel 2
// dip_switches_16bits port, all inputs
#define SW_DATA_REG             0x08
#define SW_0                    0x0001  // SW0 = rightmost = start / continue
#define SW_1                    0x0002  // SW1 = soft drop (edge triggered: flip ON = one drop)

PmodBT2 bt2;
PmodKYPD kypd;

static u32 btn_prev_state = 0;
static u32 sw_prev_state  = 0;

typedef enum
{
    DEV_NONE = 0,
    DEV_UART,
    DEV_BT2,
    DEV_KYPD,
    DEV_BTN
} DeviceType;

typedef struct
{
    DeviceType dev;
} PlayerBinding;

// --- UART P1 ---
int uart_read(u8 *c)
{
    if (Xil_In32(UART_P1_BASEADDR + UART_STAT_REG) & UART_STAT_RX_VALID)
    {
        *c = (u8)Xil_In32(UART_P1_BASEADDR + UART_RX_FIFO);
        return 1;
    }
    return 0;
}

void uart_write(u8 c)
{
    while (Xil_In32(UART_P1_BASEADDR + UART_STAT_REG) & UART_STAT_TX_FULL);
    Xil_Out32(UART_P1_BASEADDR + UART_TX_FIFO, (u32)c);
}

void uart_print(const char *s)
{
    while (*s) uart_write((u8)*s++);
}

// --- BT2 UART ---
int bt2_read(u8 *c)
{
    if (XUartNs550_IsReceiveData(BT2_UART_BASEADDR))
    {
        *c = XUartNs550_RecvByte(BT2_UART_BASEADDR);
        return 1;
    }
    return 0;
}

// --- Basys3 Buttons (edge detect) ---
u32 btn_get_pressed(void)
{
    u32 curr    = Xil_In32(BTN_BASEADDR + BTN_DATA_REG) & 0x0F;
    u32 pressed = curr & ~btn_prev_state;
    btn_prev_state = curr;
    return pressed;
}

// --- Basys3 Switches (edge detect - rising edge only) ---
// SW0: start/continue    SW1: soft drop
u32 sw_get_flipped_on(void)
{
    u32 curr    = Xil_In32(BTN_BASEADDR + SW_DATA_REG) & 0x0003;
    u32 flipped = curr & ~sw_prev_state;
    sw_prev_state = curr;
    return flipped;
}

// --- Game state ---
u8 board1[HEIGHT][WIDTH];
u8 board2[HEIGHT][WIDTH];
int score1 = 0;
int score2 = 0;
int dead1  = 0;
int dead2  = 0;
int frame_delay = FRAME_DELAY_START;

typedef struct
{
    u8 shape[4][4];
    int x;
    int y;
} Piece;

Piece p1;
Piece p2;

const u8 shapes[7][4][4] =
{
    {{1,1,1,1},{0,0,0,0},{0,0,0,0},{0,0,0,0}},
    {{1,1,0,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
    {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
    {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}
};

PlayerBinding player1 = {DEV_NONE};
PlayerBinding player2 = {DEV_NONE};

void uart_print_int(int n)
{
    char buf[12];
    int i = 0, neg = 0;
    if (n == 0) { uart_write('0'); return; }
    if (n < 0)  { neg = 1; n = -n; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    if (neg) buf[i++] = '-';
    while (i--) uart_write((u8)buf[i]);
}

void reset_cursor(void) { uart_print("\033[H"); }
void clear_screen(void) { uart_print("\033[2J"); reset_cursor(); }

void reset_game_state(void)
{
    int y, x;
    for (y = 0; y < HEIGHT; y++)
        for (x = 0; x < WIDTH; x++)
        {
            board1[y][x] = 0;
            board2[y][x] = 0;
        }
    score1 = 0; score2 = 0;
    dead1  = 0; dead2  = 0;
    p1.x = p1.y = 0;
    p2.x = p2.y = 0;
    btn_prev_state = Xil_In32(BTN_BASEADDR + BTN_DATA_REG) & 0x0F;
    sw_prev_state  = Xil_In32(BTN_BASEADDR + SW_DATA_REG)  & 0x0003;
    frame_delay    = FRAME_DELAY_START;
}

void spawn(Piece *p, u8 board[HEIGHT][WIDTH], int *dead)
{
    int type = rand() % 7, y, x;
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            p->shape[y][x] = shapes[type][y][x];
    p->x = WIDTH / 2 - 2;
    p->y = 0;
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            if (p->shape[y][x] && board[p->y + y][p->x + x])
                *dead = 1;
}

int collision(Piece *p, u8 board[HEIGHT][WIDTH], int nx, int ny)
{
    int y, x;
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            if (p->shape[y][x])
            {
                int bx = nx + x, by = ny + y;
                if (bx < 0 || bx >= WIDTH || by >= HEIGHT) return 1;
                if (by >= 0 && board[by][bx]) return 1;
            }
    return 0;
}

void place(Piece *p, u8 board[HEIGHT][WIDTH])
{
    int y, x;
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            if (p->shape[y][x])
                board[p->y + y][p->x + x] = 1;
}

int clear_lines(u8 board[HEIGHT][WIDTH], int *score)
{
    int y, x, yy, cleared = 0;
    for (y = HEIGHT - 1; y >= 0; y--)
    {
        int full = 1;
        for (x = 0; x < WIDTH; x++)
            if (!board[y][x]) full = 0;
        if (full)
        {
            for (yy = y; yy > 0; yy--)
                for (x = 0; x < WIDTH; x++)
                    board[yy][x] = board[yy - 1][x];
            for (x = 0; x < WIDTH; x++)
                board[0][x] = 0;
            *score += 100;
            cleared++;
            y++;
        }
    }
    return cleared;
}

void add_garbage(u8 board[HEIGHT][WIDTH], int lines)
{
    int y, x, i;
    for (i = 0; i < lines; i++)
    {
        for (y = 0; y < HEIGHT - 1; y++)
            for (x = 0; x < WIDTH; x++)
                board[y][x] = board[y + 1][x];
        {
            int hole = rand() % WIDTH;
            for (x = 0; x < WIDTH; x++)
                board[HEIGHT - 1][x] = (x == hole) ? 0 : 1;
        }
    }
}

void try_rotate(Piece *p, u8 board[HEIGHT][WIDTH])
{
    u8 temp[4][4];
    int y, x;
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            temp[y][x] = p->shape[3 - x][y];
    {
        Piece test = *p;
        for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++)
                test.shape[y][x] = temp[y][x];
        if (!collision(&test, board, test.x,   test.y)) {             *p = test; return; }
        if (!collision(&test, board, test.x+1, test.y)) { test.x+=1; *p = test; return; }
        if (!collision(&test, board, test.x-1, test.y)) { test.x-=1; *p = test; return; }
        if (!collision(&test, board, test.x+2, test.y)) { test.x+=2; *p = test; return; }
        if (!collision(&test, board, test.x-2, test.y)) { test.x-=2; *p = test; return; }
    }
}

void hard_drop(Piece *p, u8 board[HEIGHT][WIDTH], int *score, u8 opp_board[HEIGHT][WIDTH])
{
    while (!collision(p, board, p->x, p->y + 1)) p->y++;
    place(p, board);
    { int cleared = clear_lines(board, score); if (cleared >= 2) add_garbage(opp_board, cleared - 1); }
}

void lock_piece_and_spawn(Piece *p, u8 board[HEIGHT][WIDTH], int *score,
                          u8 opp_board[HEIGHT][WIDTH], int *dead)
{
    place(p, board);
    { int cleared = clear_lines(board, score); if (cleared >= 2) add_garbage(opp_board, cleared - 1); }
    spawn(p, board, dead);
}

void player1_rotate(void)    { if (!dead1) try_rotate(&p1, board1); }
void player1_left(void)      { if (!dead1 && !collision(&p1, board1, p1.x - 1, p1.y)) p1.x--; }
void player1_right(void)     { if (!dead1 && !collision(&p1, board1, p1.x + 1, p1.y)) p1.x++; }
void player1_soft_drop(void)
{
    if (!dead1)
    {
        if (!collision(&p1, board1, p1.x, p1.y + 1)) p1.y++;
        else lock_piece_and_spawn(&p1, board1, &score1, board2, &dead1);
    }
}
void player1_hard_drop(void)
{
    if (!dead1) { hard_drop(&p1, board1, &score1, board2); spawn(&p1, board1, &dead1); }
}

void player2_rotate(void)    { if (!dead2) try_rotate(&p2, board2); }
void player2_left(void)      { if (!dead2 && !collision(&p2, board2, p2.x - 1, p2.y)) p2.x--; }
void player2_right(void)     { if (!dead2 && !collision(&p2, board2, p2.x + 1, p2.y)) p2.x++; }
void player2_soft_drop(void)
{
    if (!dead2)
    {
        if (!collision(&p2, board2, p2.x, p2.y + 1)) p2.y++;
        else lock_piece_and_spawn(&p2, board2, &score2, board1, &dead2);
    }
}
void player2_hard_drop(void)
{
    if (!dead2) { hard_drop(&p2, board2, &score2, board1); spawn(&p2, board2, &dead2); }
}

int is_keyboard_game_key(u8 c)
{
    return (c=='a'||c=='A'||c=='d'||c=='D'||c=='s'||c=='S'||
            c=='r'||c=='R'||c=='e'||c=='E'||c=='p'||c=='P');
}

int is_kypd_game_key(u8 c)
{
    return (c=='5'||c=='B'||c=='8'||c=='A'||c=='F'||c=='2');
}

char translate_kypd_to_virtual_key(u8 c)
{
    if (c == '5') return 'a'; // left
    if (c == 'B') return 'd'; // right
    if (c == '8') return 's'; // soft drop
    if (c == 'F') return 'e'; // hard drop
    if (c == 'A') return 'r'; // rotate
    if (c == '2') return 'p'; // continue/start
    return 0;
}

int poll_uart_event(u8 *c)
{
    if (uart_read(c) && is_keyboard_game_key(*c)) return 1;
    return 0;
}

int poll_bt2_event(u8 *c)
{
    if (bt2_read(c) && is_keyboard_game_key(*c)) return 1;
    return 0;
}

int poll_kypd_event(u8 *c)
{
    static int key_latched = 0;
    static u8 latched_key  = 0;
    u16 keystate;
    XStatus status;
    u8 key = 0;

    keystate = KYPD_getKeyStates(&kypd);
    status   = KYPD_getKeyPressed(&kypd, keystate, &key);

    if (status == KYPD_SINGLE_KEY)
    {
        if (!key_latched)
        {
            key_latched = 1;
            latched_key = key;
            if (is_kypd_game_key(latched_key))
            {
                *c = latched_key;
                return 1;
            }
        }
    }
    else if (status == KYPD_NO_KEY)
    {
        key_latched = 0;
        latched_key = 0;
    }
    return 0;
}

void drain_inputs(void)
{
    u8 c = 0;
    int i;
    usleep(100000);
    while (uart_read(&c));
    while (bt2_read(&c));
    for (i = 0; i < 20; i++) { poll_kypd_event(&c); usleep(5000); }
    btn_prev_state = Xil_In32(BTN_BASEADDR + BTN_DATA_REG) & 0x0F;
    sw_prev_state  = Xil_In32(BTN_BASEADDR + SW_DATA_REG)  & 0x0003;
}

const char *device_name(DeviceType dev)
{
    switch (dev)
    {
        case DEV_UART: return "WIRED KEYBOARD";
        case DEV_BT2:  return "BT2 KEYBOARD";
        case DEV_KYPD: return "PMOD KEYPAD";
        case DEV_BTN:  return "BASYS3 BUTTONS";
        default:       return "NONE";
    }
}

void print_device_controls(DeviceType dev)
{
    switch (dev)
    {
        case DEV_UART:
        case DEV_BT2:
            uart_print("  R = Rotate\r\n");
            uart_print("  A = Move Left\r\n");
            uart_print("  D = Move Right\r\n");
            uart_print("  S = Soft Drop\r\n");
            uart_print("  E = Hard Drop\r\n");
            uart_print("  P = Continue / Return Home\r\n");
            break;
        case DEV_KYPD:
            uart_print("  A = Rotate\r\n");
            uart_print("  5 = Move Left\r\n");
            uart_print("  B = Move Right\r\n");
            uart_print("  8 = Soft Drop\r\n");
            uart_print("  F = Hard Drop\r\n");
            uart_print("  2 = Continue / Return Home\r\n");
            break;
        case DEV_BTN:
            uart_print("  BTNU  = Rotate\r\n");
            uart_print("  BTNL  = Move Left\r\n");
            uart_print("  BTNR  = Move Right\r\n");
            uart_print("  BTND  = Hard Drop\r\n");
            uart_print("  SW1   = Soft Drop (flip ON)\r\n");
            uart_print("  SW0   = Continue / Return Home (flip ON)\r\n");
            break;
        default:
            uart_print("  (unassigned)\r\n");
            break;
    }
}

void apply_player1_action_keyboard(u8 c)
{
    if      (c == 'r' || c == 'R') player1_rotate();
    else if (c == 'e' || c == 'E') player1_hard_drop();
    else if (c == 'a' || c == 'A') player1_left();
    else if (c == 'd' || c == 'D') player1_right();
    else if (c == 's' || c == 'S') player1_soft_drop();
}

void apply_player2_action_keyboard(u8 c)
{
    if      (c == 'r' || c == 'R') player2_rotate();
    else if (c == 'e' || c == 'E') player2_hard_drop();
    else if (c == 'a' || c == 'A') player2_left();
    else if (c == 'd' || c == 'D') player2_right();
    else if (c == 's' || c == 'S') player2_soft_drop();
}

void apply_btn_actions(u32 btn_pressed, u32 sw_flipped, int player)
{
    if (player == 1)
    {
        if (btn_pressed & BTN_UP)    player1_rotate();
        if (btn_pressed & BTN_LEFT)  player1_left();
        if (btn_pressed & BTN_RIGHT) player1_right();
        if (btn_pressed & BTN_DOWN)  player1_hard_drop();
        if (sw_flipped  & SW_1)      player1_soft_drop();
    }
    else
    {
        if (btn_pressed & BTN_UP)    player2_rotate();
        if (btn_pressed & BTN_LEFT)  player2_left();
        if (btn_pressed & BTN_RIGHT) player2_right();
        if (btn_pressed & BTN_DOWN)  player2_hard_drop();
        if (sw_flipped  & SW_1)      player2_soft_drop();
    }
}

void handle_input(void)
{
    u8 c;
    char vk;
    u32 btn_pressed, sw_flipped;

    if (player1.dev == DEV_UART)
        while (poll_uart_event(&c)) apply_player1_action_keyboard(c);

    if (player2.dev == DEV_UART)
        while (poll_uart_event(&c)) apply_player2_action_keyboard(c);

    if (player1.dev == DEV_BT2)
        while (poll_bt2_event(&c)) apply_player1_action_keyboard(c);

    if (player2.dev == DEV_BT2)
        while (poll_bt2_event(&c)) apply_player2_action_keyboard(c);

    if (player1.dev == DEV_KYPD)
    {
        if (poll_kypd_event(&c))
        {
            vk = translate_kypd_to_virtual_key(c);
            if (vk) apply_player1_action_keyboard((u8)vk);
        }
    }

    if (player2.dev == DEV_KYPD)
    {
        if (poll_kypd_event(&c))
        {
            vk = translate_kypd_to_virtual_key(c);
            if (vk) apply_player2_action_keyboard((u8)vk);
        }
    }

    if (player1.dev == DEV_BTN || player2.dev == DEV_BTN)
    {
        btn_pressed = btn_get_pressed();
        sw_flipped  = sw_get_flipped_on();

        if (player1.dev == DEV_BTN) apply_btn_actions(btn_pressed, sw_flipped, 1);
        if (player2.dev == DEV_BTN) apply_btn_actions(btn_pressed, sw_flipped, 2);
    }
}

void service_inputs_burst(int polls, int delay_us)
{
    int i;
    for (i = 0; i < polls; i++) { handle_input(); usleep(delay_us); }
}

void draw(void)
{
    int y, x;
    reset_cursor();
    uart_print("PLAYER 1              PLAYER 2\r\n");
    uart_print("Score:"); uart_print_int(score1);
    uart_print("                Score:"); uart_print_int(score2); uart_print("\r\n");
    uart_print("+--------------------+  +--------------------+\r\n");
    for (y = 0; y < HEIGHT; y++)
    {
        uart_write('|');
        for (x = 0; x < WIDTH; x++)
        {
            int drawn = 0, py, px;
            for (py = 0; py < 4; py++)
                for (px = 0; px < 4; px++)
                    if (p1.shape[py][px] && p1.x + px == x && p1.y + py == y)
                        drawn = 1;
            if (drawn)             uart_print("[]");
            else if (board1[y][x]) uart_print("[]");
            else                   uart_print("  ");
        }
        uart_print("|  |");
        for (x = 0; x < WIDTH; x++)
        {
            int drawn = 0, py, px;
            for (py = 0; py < 4; py++)
                for (px = 0; px < 4; px++)
                    if (p2.shape[py][px] && p2.x + px == x && p2.y + py == y)
                        drawn = 1;
            if (drawn)             uart_print("[]");
            else if (board2[y][x]) uart_print("[]");
            else                   uart_print("  ");
        }
        uart_print("|\r\n");
    }
    uart_print("+--------------------+  +--------------------+\r\n");
}

int claim_device(PlayerBinding *slot, DeviceType dev)
{
    if (slot->dev == DEV_NONE) { slot->dev = dev; return 1; }
    return 0;
}

int device_already_claimed(DeviceType dev)
{
    return (player1.dev == dev || player2.dev == dev);
}

void print_continue_key_for_device(DeviceType dev)
{
    switch (dev)
    {
        case DEV_KYPD: uart_print("2");   break;
        case DEV_BTN:  uart_print("SW0"); break;
        default:       uart_print("P");   break;
    }
}

void draw_connect_screen(void)
{
    clear_screen();
    uart_print("\r\n\r\n");
    uart_print("  ========================================\r\n");
    uart_print("  ||         CONNECT CONTROLLERS        ||\r\n");
    uart_print("  ========================================\r\n\r\n");
    uart_print("  First two DIFFERENT devices to send input\r\n");
    uart_print("  become Player 1 and Player 2.\r\n\r\n");
    uart_print("  Wired/BT2 keys : A D S R E P\r\n");
    uart_print("  Keypad keys    : 5 B 8 A F 2\r\n");
    uart_print("  Basys3 buttons : press any button\r\n\r\n");
    uart_print("  P1: "); uart_print(device_name(player1.dev)); uart_print("\r\n");
    uart_print("  P2: "); uart_print(device_name(player2.dev)); uart_print("\r\n\r\n");
    if (player1.dev == DEV_NONE || player2.dev == DEV_NONE)
    {
        uart_print("  Waiting for two controllers...\r\n");
    }
    else
    {
        uart_print("  Controllers connected!\r\n\r\n");
        uart_print("  PRESS ");
        print_continue_key_for_device(player1.dev);
        uart_print(" OR ");
        print_continue_key_for_device(player2.dev);
        uart_print(" TO CONTINUE\r\n");
    }
}

int sw0_just_flipped(void)
{
    u32 flipped = sw_get_flipped_on();
    return (flipped & SW_0) ? 1 : 0;
}

void wait_for_assignment_continue(void)
{
    u8 c = 0;
    int go = 0;
    u32 pressed;
    drain_inputs();
    while (!go)
    {
        if (player1.dev == DEV_UART && poll_uart_event(&c) && (c == 'p' || c == 'P')) go = 1;
        if (player2.dev == DEV_UART && poll_uart_event(&c) && (c == 'p' || c == 'P')) go = 1;
        if (player1.dev == DEV_BT2  && poll_bt2_event(&c)  && (c == 'p' || c == 'P')) go = 1;
        if (player2.dev == DEV_BT2  && poll_bt2_event(&c)  && (c == 'p' || c == 'P')) go = 1;
        if (player1.dev == DEV_KYPD && poll_kypd_event(&c) && c == '2') go = 1;
        if (player2.dev == DEV_KYPD && poll_kypd_event(&c) && c == '2') go = 1;
        pressed = btn_get_pressed();
        if ((player1.dev == DEV_BTN || player2.dev == DEV_BTN) && sw0_just_flipped()) go = 1;
        (void)pressed;
        usleep(1000);
    }
}

void wait_for_player_assignment(void)
{
    u8 c;
    u32 pressed;
    player1.dev = DEV_NONE;
    player2.dev = DEV_NONE;
    drain_inputs();
    draw_connect_screen();
    while (player1.dev == DEV_NONE || player2.dev == DEV_NONE)
    {
        if (poll_uart_event(&c) && !device_already_claimed(DEV_UART))
            if (claim_device(&player1, DEV_UART) || claim_device(&player2, DEV_UART))
                draw_connect_screen();

        if (poll_bt2_event(&c) && !device_already_claimed(DEV_BT2))
            if (claim_device(&player1, DEV_BT2) || claim_device(&player2, DEV_BT2))
                draw_connect_screen();

        if (poll_kypd_event(&c) && !device_already_claimed(DEV_KYPD))
            if (claim_device(&player1, DEV_KYPD) || claim_device(&player2, DEV_KYPD))
                draw_connect_screen();

        pressed = btn_get_pressed();
        if (pressed && !device_already_claimed(DEV_BTN))
            if (claim_device(&player1, DEV_BTN) || claim_device(&player2, DEV_BTN))
                draw_connect_screen();

        usleep(1000);
    }
    draw_connect_screen();
    wait_for_assignment_continue();
}

void draw_home_menu(void)
{
    clear_screen();
    uart_print("\r\n\r\n");
    uart_print("  ========================================\r\n");
    uart_print("  ||         UART TETRIS 2P             ||\r\n");
    uart_print("  ========================================\r\n\r\n");
    uart_print("  PLAYER 1 DEVICE: "); uart_print(device_name(player1.dev)); uart_print("\r\n");
    print_device_controls(player1.dev);
    uart_print("\r\n");
    uart_print("  PLAYER 2 DEVICE: "); uart_print(device_name(player2.dev)); uart_print("\r\n");
    print_device_controls(player2.dev);
    uart_print("\r\n");
    uart_print("  Clear 2+ lines to send garbage rows!\r\n\r\n");
    uart_print("  PRESS ");
    print_continue_key_for_device(player1.dev);
    uart_print(" OR ");
    print_continue_key_for_device(player2.dev);
    uart_print(" TO START\r\n");
}

int is_start_key_keyboard(u8 c) { return (c == 'p' || c == 'P'); }
int is_start_key_kypd(u8 c)     { return (c == '2'); }

void wait_for_start(void)
{
    u8 c = 0;
    int started = 0;
    drain_inputs();
    while (!started)
    {
        if (player1.dev == DEV_UART && poll_uart_event(&c) && is_start_key_keyboard(c)) started = 1;
        if (player2.dev == DEV_UART && poll_uart_event(&c) && is_start_key_keyboard(c)) started = 1;
        if (player1.dev == DEV_BT2  && poll_bt2_event(&c)  && is_start_key_keyboard(c)) started = 1;
        if (player2.dev == DEV_BT2  && poll_bt2_event(&c)  && is_start_key_keyboard(c)) started = 1;
        if (player1.dev == DEV_KYPD && poll_kypd_event(&c) && is_start_key_kypd(c)) started = 1;
        if (player2.dev == DEV_KYPD && poll_kypd_event(&c) && is_start_key_kypd(c)) started = 1;
        if ((player1.dev == DEV_BTN || player2.dev == DEV_BTN) && sw0_just_flipped()) started = 1;
        usleep(1000);
    }
}

void game_over_screen(void)
{
    clear_screen();
    uart_print("\r\n\r\n");
    uart_print("  ================================\r\n");
    uart_print("           GAME OVER\r\n");
    uart_print("  ================================\r\n\r\n");
    if (dead1 && dead2)
    {
        if      (score1 > score2) { uart_print("     PLAYER 1 WINS! ("); uart_print_int(score1); uart_print(" vs "); uart_print_int(score2); uart_print(")\r\n"); }
        else if (score2 > score1) { uart_print("     PLAYER 2 WINS! ("); uart_print_int(score2); uart_print(" vs "); uart_print_int(score1); uart_print(")\r\n"); }
        else                      { uart_print("     IT'S A TIE! ("); uart_print_int(score1); uart_print(")\r\n"); }
    }
    else if (dead1) { uart_print("     PLAYER 2 WINS! ("); uart_print_int(score2); uart_print(" vs "); uart_print_int(score1); uart_print(")\r\n"); }
    else            { uart_print("     PLAYER 1 WINS! ("); uart_print_int(score1); uart_print(" vs "); uart_print_int(score2); uart_print(")\r\n"); }
    uart_print("\r\n  ================================\r\n");
    uart_print("  P1 Score: "); uart_print_int(score1); uart_print("\r\n");
    uart_print("  P2 Score: "); uart_print_int(score2); uart_print("\r\n");
    uart_print("  ================================\r\n\r\n");
    uart_print("  PRESS ");
    print_continue_key_for_device(player1.dev);
    uart_print(" OR ");
    print_continue_key_for_device(player2.dev);
    uart_print(" TO RETURN HOME\r\n");
}

void wait_for_restart(void)
{
    u8 c = 0;
    int restart = 0;
    drain_inputs();
    while (!restart)
    {
        if (player1.dev == DEV_UART && poll_uart_event(&c) && is_start_key_keyboard(c)) restart = 1;
        if (player2.dev == DEV_UART && poll_uart_event(&c) && is_start_key_keyboard(c)) restart = 1;
        if (player1.dev == DEV_BT2  && poll_bt2_event(&c)  && is_start_key_keyboard(c)) restart = 1;
        if (player2.dev == DEV_BT2  && poll_bt2_event(&c)  && is_start_key_keyboard(c)) restart = 1;
        if (player1.dev == DEV_KYPD && poll_kypd_event(&c) && is_start_key_kypd(c)) restart = 1;
        if (player2.dev == DEV_KYPD && poll_kypd_event(&c) && is_start_key_kypd(c)) restart = 1;
        if ((player1.dev == DEV_BTN || player2.dev == DEV_BTN) && sw0_just_flipped()) restart = 1;
        usleep(1000);
    }
}

int main(void)
{
    init_platform();

    BT2_Begin(&bt2, BT2_GPIO_BASEADDR, BT2_UART_BASEADDR,
              XPAR_CPU_M_AXI_DP_FREQ_HZ, BT2_BAUD);

    KYPD_begin(&kypd, KYPD_GPIO_BASEADDR);
    KYPD_loadKeyTable(&kypd, (u8*)DEFAULT_KEYTABLE);
    Xil_Out32(kypd.GPIO_addr, 0xF);

    Xil_Out32(BTN_BASEADDR + 0x04, 0x0F);
    Xil_Out32(BTN_BASEADDR + 0x0C, 0xFFFF);

    srand(1);

    while (1)
    {
        int gravity_timer = 0;
        int frame_counter = 0;
        reset_game_state();
        wait_for_player_assignment();
        draw_home_menu();
        wait_for_start();
        clear_screen();
        spawn(&p1, board1, &dead1);
        spawn(&p2, board2, &dead2);

        while (!dead1 && !dead2)
        {
            service_inputs_burst(8, frame_delay / 8);
            gravity_timer++;
            frame_counter++;

            // Speed up every SPEED_INTERVAL frames
            if (frame_counter >= SPEED_INTERVAL)
            {
                frame_counter = 0;
                if (frame_delay > FRAME_DELAY_MIN)
                {
                    frame_delay -= 30;
                    if (frame_delay < FRAME_DELAY_MIN) frame_delay = FRAME_DELAY_MIN;
                }
            }

            if (gravity_timer >= GRAVITY_TICKS)
            {
                gravity_timer = 0;
                if (!dead1)
                {
                    if (!collision(&p1, board1, p1.x, p1.y + 1)) p1.y++;
                    else lock_piece_and_spawn(&p1, board1, &score1, board2, &dead1);
                }
                if (!dead2)
                {
                    if (!collision(&p2, board2, p2.x, p2.y + 1)) p2.y++;
                    else lock_piece_and_spawn(&p2, board2, &score2, board1, &dead2);
                }
            }
            draw();
            service_inputs_burst(4, frame_delay / 4);
        }

        game_over_screen();
        wait_for_restart();
    }

    cleanup_platform();
    return 0;
}
