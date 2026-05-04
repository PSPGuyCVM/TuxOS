/* kernel.c - TuxOS 0.2.1 */

#define VIDEO_MEMORY 0xB8000
#define MAX_ROWS 25
#define MAX_COLS 80
#define WHITE_ON_BLACK 0x0F
#define BLUE_BG       0x1F   /* white on blue */

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

static int cursor_row = 0;
static int cursor_col = 0;

/* --- Low‑level I/O ports --- */
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(unsigned short port, unsigned char data) {
    asm volatile ("outb %0, %1" :: "a"(data), "Nd"(port));
}

static inline void outw(unsigned short port, unsigned short data) {
    asm volatile ("outw %0, %1" :: "a"(data), "Nd"(port));
}

/* --- VGA text-mode helpers --- */
static char *get_video_ptr(int row, int col) {
    return (char *)(VIDEO_MEMORY + 2 * (row * MAX_COLS + col));
}

void clear_screen() {
    for (int i = 0; i < MAX_ROWS * MAX_COLS; i++) {
        char *ptr = (char *)(VIDEO_MEMORY + 2 * i);
        *ptr = ' ';
        *(ptr + 1) = WHITE_ON_BLACK;
    }
    cursor_row = 0;
    cursor_col = 0;
}

static void scroll_up() {
    for (int r = 1; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            char *dst = get_video_ptr(r - 1, c);
            char *src = get_video_ptr(r, c);
            *dst = *src;
            *(dst + 1) = *(src + 1);
        }
    }
    for (int c = 0; c < MAX_COLS; c++) {
        char *ptr = get_video_ptr(MAX_ROWS - 1, c);
        *ptr = ' ';
        *(ptr + 1) = WHITE_ON_BLACK;
    }
}

void print_char(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else {
        char *ptr = get_video_ptr(cursor_row, cursor_col);
        *ptr = c;
        *(ptr + 1) = WHITE_ON_BLACK;
        cursor_col++;
    }

    if (cursor_col >= MAX_COLS) {
        cursor_col = 0;
        cursor_row++;
    }
    if (cursor_row >= MAX_ROWS) {
        cursor_row = MAX_ROWS - 1;
        scroll_up();
    }

    unsigned short pos = cursor_row * MAX_COLS + cursor_col;
    outb(0x3D4, 14);
    outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 15);
    outb(0x3D5, pos & 0xFF);
}

void print_string(const char *str) {
    while (*str) print_char(*str++);
}

/* --- Integer to string conversion --- */
static void reverse_str(char *s, int len) {
    for (int i = 0; i < len / 2; i++) {
        char tmp = s[i];
        s[i] = s[len - 1 - i];
        s[len - 1 - i] = tmp;
    }
}

void int_to_str(int num, char *buf) {
    int i = 0;
    int sign = 0;
    if (num < 0) {
        sign = 1;
        num = -num;
    }
    do {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    } while (num > 0);
    if (sign) buf[i++] = '-';
    buf[i] = '\0';
    reverse_str(buf, i);
}

/* --- KERNEL PANIC --- */
void kernel_panic(const char *reason) {
    /* fill screen with blue background */
    for (int i = 0; i < MAX_ROWS * MAX_COLS; i++) {
        char *ptr = (char *)(VIDEO_MEMORY + 2 * i);
        *ptr = ' ';
        *(ptr + 1) = BLUE_BG;
    }

    /* main message */
    const char *msg = "KERNEL PANIC :(";
    int len = 0;
    while (msg[len]) len++;
    int msg_row = 10;
    int msg_col = (MAX_COLS - len) / 2;
    for (int i = 0; i < len; i++) {
        char *p = get_video_ptr(msg_row, msg_col + i);
        *p = msg[i];
        *(p + 1) = BLUE_BG;
    }

    /* reason at bottom center */
    int reason_len = 0;
    while (reason[reason_len]) reason_len++;
    int r_row = 23;
    int r_col = (MAX_COLS - reason_len) / 2;
    if (r_col < 0) r_col = 0;
    for (int i = 0; i < reason_len; i++) {
        char *p = get_video_ptr(r_row, r_col + i);
        *p = reason[i];
        *(p + 1) = BLUE_BG;
    }

    /* halt forever */
    asm volatile ("cli; hlt");
    while (1) {}
}

/* --- Keyboard input with shift --- */
static const char scancode_lower[] = {
    0,0,'1','2','3','4','5','6','7','8','9','0','-','=',0,
    0,'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,
    '\\','z','x','c','v','b','n','m',',','.','/',0,0,0,' '
};

static const char scancode_upper[] = {
    0,0,'!','@','#','$','%','^','&','*','(',')','_','+',0,
    0,'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,
    '|','Z','X','C','V','B','N','M','<','>','?',0,0,0,' '
};

int read_line(char *buffer, int max) {
    int pos = 0;
    int shift = 0;
    while (1) {
        while (!(inb(KEYBOARD_STATUS_PORT) & 0x01));
        unsigned char sc = inb(KEYBOARD_DATA_PORT);

        if (sc & 0x80) {
            sc &= 0x7F;
            if (sc == 0x2A || sc == 0x36) shift = 0;
            continue;
        }
        if (sc == 0x2A || sc == 0x36) { shift = 1; continue; }

        if (sc == 0x0E) {          /* backspace */
            if (pos > 0 && cursor_col > 0) {
                pos--;
                cursor_col--;
                char *ptr = get_video_ptr(cursor_row, cursor_col);
                *ptr = ' ';
                *(ptr + 1) = WHITE_ON_BLACK;
                unsigned short cur = cursor_row * MAX_COLS + cursor_col;
                outb(0x3D4, 14); outb(0x3D5, (cur >> 8) & 0xFF);
                outb(0x3D4, 15); outb(0x3D5, cur & 0xFF);
            }
        } else if (sc == 0x1C) {   /* enter */
            print_char('\n');
            buffer[pos] = '\0';
            return pos;
        } else if (sc < sizeof(scancode_lower)) {
            char ascii = shift ? scancode_upper[sc] : scancode_lower[sc];
            if (ascii && pos < max - 1) {
                buffer[pos++] = ascii;
                print_char(ascii);
            }
        }
    }
}

/* --- String utilities --- */
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

int strlen(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

/* --- Decimal to hex print --- */
void print_hex(unsigned int num) {
    char buf[16];
    int i = 0;
    if (num == 0) { print_string("0"); return; }
    do {
        int digit = num % 16;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        num /= 16;
    } while (num > 0);
    buf[i] = '\0';
    for (int j = 0; j < i/2; j++) {
        char t = buf[j];
        buf[j] = buf[i-1-j];
        buf[i-1-j] = t;
    }
    print_string(buf);
}

/* --- Calculator --- */
int calc_expression(const char *expr) {
    int num1 = 0, num2 = 0;
    char op = 0;
    int i = 0;
    while (expr[i] == ' ') i++;
    while (expr[i] >= '0' && expr[i] <= '9') {
        num1 = num1 * 10 + (expr[i] - '0');
        i++;
    }
    if (expr[i] == '+' || expr[i] == '-' || expr[i] == '*' || expr[i] == '/') {
        op = expr[i];
        i++;
    } else return 0;
    while (expr[i] >= '0' && expr[i] <= '9') {
        num2 = num2 * 10 + (expr[i] - '0');
        i++;
    }
    switch (op) {
        case '+': return num1 + num2;
        case '-': return num1 - num2;
        case '*': return num1 * num2;
        case '/': if (num2) return num1 / num2; else return 0;
        default: return 0;
    }
}

/* --- ASCII table --- */
void print_ascii_table() {
    for (int i = 32; i < 127; i += 8) {
        for (int j = 0; j < 8 && (i+j) < 127; j++) {
            char buf[8];
            buf[0] = i+j;
            buf[1] = ' ';
            buf[2] = '\0';
            print_string(buf);
        }
        print_string("\n");
    }
}

/* --- Cow --- */
void cow_say(const char *msg) {
    int len = strlen(msg);
    print_char(' ');
    for (int i = 0; i < len + 2; i++) print_char('_');
    print_string("\n< ");
    print_string(msg);
    print_string(" >\n ");
    for (int i = 0; i < len + 2; i++) print_char('-');
    print_string("\n        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||\n");
}

/* --- Fortune --- */
void print_fortune() {
    const char *fortunes[] = {
        "You will write a cool OS.",
        "Bug is just a feature waiting to be found.",
        "Tux is silently judging your code.",
        "Real programmers use butterflies.",
        "Segmentation fault (core dumped) ... just kidding.",
        "The answer is 42."
    };
    int n = sizeof(fortunes) / sizeof(fortunes[0]);
    static int counter = 0;
    int idx = counter++ % n;
    print_string(fortunes[idx]);
    print_string("\n");
}

/* ===========================================================
   = PONG: static helper functions (no lambdas)
   =========================================================== */
static void draw_paddle(int col, int y) {
    for (int i = 0; i < 4; i++) {
        int r = y + i;
        if (r > 0 && r < MAX_ROWS-1) {
            char *p = get_video_ptr(r, col);
            *p = '|';
            *(p+1) = 0x0F;
        }
    }
}

static void clear_paddle(int col, int y) {
    for (int i = 0; i < 4; i++) {
        int r = y + i;
        if (r > 0 && r < MAX_ROWS-1) {
            char *p = get_video_ptr(r, col);
            *p = ' ';
            *(p+1) = 0x0F;
        }
    }
}

static void draw_ball(int x, int y) {
    if (y > 0 && y < MAX_ROWS-1 && x > 0 && x < MAX_COLS-1) {
        char *p = get_video_ptr(y, x);
        *p = 'O';
        *(p+1) = 0x0F;
    }
}

static void clear_ball(int x, int y) {
    if (y > 0 && y < MAX_ROWS-1 && x > 0 && x < MAX_COLS-1) {
        char *p = get_video_ptr(y, x);
        *p = ' ';
        *(p+1) = 0x0F;
    }
}

/* --- Non‑blocking keyboard for games --- */
static int kbhit() {
    return inb(KEYBOARD_STATUS_PORT) & 0x01;
}

static int get_scancode() {
    if (kbhit())
        return inb(KEYBOARD_DATA_PORT);
    return -1;
}

/* --- Pong game loop --- */
void pong_game() {
    /* clear screen with black background */
    for (int i = 0; i < MAX_ROWS * MAX_COLS; i++) {
        char *p = (char*)(VIDEO_MEMORY + 2*i);
        *p = ' ';
        *(p+1) = 0x0F;
    }

    /* draw borders */
    for (int c = 0; c < MAX_COLS; c++) {
        char *top = get_video_ptr(0, c);
        *top = '-';
        *(top+1) = 0x0F;
        char *bot = get_video_ptr(MAX_ROWS-1, c);
        *bot = '-';
        *(bot+1) = 0x0F;
    }
    for (int r = 1; r < MAX_ROWS-1; r++) {
        char *left = get_video_ptr(r, 0);
        *left = '|';
        *(left+1) = 0x0F;
        char *right = get_video_ptr(r, MAX_COLS-1);
        *right = '|';
        *(right+1) = 0x0F;
    }

    int left_y   = MAX_ROWS/2 - 2;
    int right_y  = MAX_ROWS/2 - 2;
    int ball_x = MAX_COLS/2, ball_y = MAX_ROWS/2;
    int ball_dx = 1, ball_dy = 1;
    int score_left = 0, score_right = 0;

    draw_paddle(2, left_y);
    draw_paddle(77, right_y);
    draw_ball(ball_x, ball_y);

    while (1) {
        /* slow down */
        for (volatile int i = 0; i < 500000; i++);

        /* read keyboard */
        int sc;
        while ((sc = get_scancode()) != -1) {
            if (sc & 0x80) continue;
            if (sc == 0x10) {          /* Q to quit */
                clear_screen();
                return;
            }
            /* left paddle: W/S */
            if (sc == 0x11) {          /* W */
                if (left_y > 1) {
                    clear_paddle(2, left_y);
                    left_y--;
                    draw_paddle(2, left_y);
                }
            }
            if (sc == 0x1F) {          /* S */
                if (left_y + 4 < MAX_ROWS-1) {
                    clear_paddle(2, left_y);
                    left_y++;
                    draw_paddle(2, left_y);
                }
            }
            /* right paddle: Up/Down arrows */
            if (sc == 0x48) {          /* Up arrow */
                if (right_y > 1) {
                    clear_paddle(77, right_y);
                    right_y--;
                    draw_paddle(77, right_y);
                }
            }
            if (sc == 0x50) {          /* Down arrow */
                if (right_y + 4 < MAX_ROWS-1) {
                    clear_paddle(77, right_y);
                    right_y++;
                    draw_paddle(77, right_y);
                }
            }
        }

        /* move ball */
        clear_ball(ball_x, ball_y);
        ball_x += ball_dx;
        ball_y += ball_dy;

        /* bounce off top/bottom */
        if (ball_y <= 1 || ball_y >= MAX_ROWS-2) {
            ball_dy = -ball_dy;
            ball_y += ball_dy;
        }

        /* paddle collision (left) */
        if (ball_x == 3) {
            if (ball_y >= left_y && ball_y < left_y + 4) {
                ball_dx = 1;
                ball_x = 3;
            }
        }
        /* paddle collision (right) */
        if (ball_x == 76) {
            if (ball_y >= right_y && ball_y < right_y + 4) {
                ball_dx = -1;
                ball_x = 76;
            }
        }

        /* scoring */
        if (ball_x <= 1) {
            score_right++;
            ball_x = MAX_COLS/2; ball_y = MAX_ROWS/2;
            ball_dx = 1; ball_dy = 1;
            /* redraw all */
            for (int i = 0; i < MAX_ROWS * MAX_COLS; i++) {
                char *p = (char*)(VIDEO_MEMORY + 2*i);
                *p = ' ';
                *(p+1) = 0x0F;
            }
            for (int c = 0; c < MAX_COLS; c++) {
                *get_video_ptr(0, c) = '-';
                *(get_video_ptr(0, c)+1) = 0x0F;
                *get_video_ptr(MAX_ROWS-1, c) = '-';
                *(get_video_ptr(MAX_ROWS-1, c)+1) = 0x0F;
            }
            for (int r = 1; r < MAX_ROWS-1; r++) {
                *get_video_ptr(r, 0) = '|';
                *(get_video_ptr(r, 0)+1) = 0x0F;
                *get_video_ptr(r, MAX_COLS-1) = '|';
                *(get_video_ptr(r, MAX_COLS-1)+1) = 0x0F;
            }
            draw_paddle(2, left_y);
            draw_paddle(77, right_y);
        } else if (ball_x >= MAX_COLS-2) {
            score_left++;
            ball_x = MAX_COLS/2; ball_y = MAX_ROWS/2;
            ball_dx = -1; ball_dy = 1;
            /* redraw all */
            for (int i = 0; i < MAX_ROWS * MAX_COLS; i++) {
                char *p = (char*)(VIDEO_MEMORY + 2*i);
                *p = ' ';
                *(p+1) = 0x0F;
            }
            for (int c = 0; c < MAX_COLS; c++) {
                *get_video_ptr(0, c) = '-';
                *(get_video_ptr(0, c)+1) = 0x0F;
                *get_video_ptr(MAX_ROWS-1, c) = '-';
                *(get_video_ptr(MAX_ROWS-1, c)+1) = 0x0F;
            }
            for (int r = 1; r < MAX_ROWS-1; r++) {
                *get_video_ptr(r, 0) = '|';
                *(get_video_ptr(r, 0)+1) = 0x0F;
                *get_video_ptr(r, MAX_COLS-1) = '|';
                *(get_video_ptr(r, MAX_COLS-1)+1) = 0x0F;
            }
            draw_paddle(2, left_y);
            draw_paddle(77, right_y);
        }

        draw_ball(ball_x, ball_y);

        /* show scores on top row */
        char buf[8];
        int_to_str(score_left, buf);
        for (int i = 0; buf[i]; i++) {
            char *p = get_video_ptr(0, 35 + i);
            *p = buf[i];
        }
        int_to_str(score_right, buf);
        for (int i = 0; buf[i]; i++) {
            char *p = get_video_ptr(0, 43 + i);
            *p = buf[i];
        }
    }
}

/* ===========================================================
   = JOKE COMMANDS
   =========================================================== */
void print_sl() {
    print_string(
        "             (@@@)\n"
        "    (------)   |   \n"
        "   (_______)| -+- |\n"
        "   /       /  |   |\n"
        "  oooooooo oooo   oo\n"
        "chuff chuff chuff...\n"
    );
}

void print_nyancat() {
    print_string(
        "+      o     +              o\n"
        "    +             +     +    \n"
        "        ,~~~.          \n"
        "       (  o o)~~~~~~~~~ \n"
        "        \\_/  \\         \n"
        "        /   \\  \\       \n"
        "       /~~~~~\\  \\      \n"
        "+     o       +    o    +\n"
        "Nyan nyan nyan...\n"
    );
}

void print_rickroll() {
    print_string(
        "Never gonna give you up\n"
        "Never gonna let you down\n"
        "Never gonna run around and desert you\n"
        "Never gonna make you cry\n"
        "Never gonna say goodbye\n"
        "Never gonna tell a lie and hurt you\n"
    );
}

/* ===========================================================
   = SHELL
   =========================================================== */
void shell() {
    char input[128];
    while (1) {
        print_string("TuxOS> ");
        int len = read_line(input, sizeof(input));
        if (len == 0) continue;

        char *cmd = input;
        char *args = "";
        for (int i = 0; i < len; i++) {
            if (input[i] == ' ') {
                input[i] = '\0';
                args = input + i + 1;
                break;
            }
        }

        if (!strcmp(cmd, "help")) {
            print_string("Commands: help, whoami, echo, clear, uname, date, ls, pwd, ver, about, tux,\n");
            print_string("shutdown, reboot, calc <expr>, hex <num>, random, ascii, cowsay <msg>,\n");
            print_string("fortune, mem, uptime, halt, panic, pong, sl, nyancat, rickroll\n");
        } else if (!strcmp(cmd, "whoami")) {
            print_string("root\n");
        } else if (!strcmp(cmd, "echo")) {
            print_string(args);
            print_string("\n");
        } else if (!strcmp(cmd, "clear")) {
            clear_screen();
        } else if (!strcmp(cmd, "uname")) {
            print_string("TuxOS\n");
        } else if (!strcmp(cmd, "date")) {
            print_string("Sun May  4 12:00:00 UTC 2026\n");
        } else if (!strcmp(cmd, "ls")) {
            print_string("No filesystem.\n");
        } else if (!strcmp(cmd, "pwd")) {
            print_string("/\n");
        } else if (!strcmp(cmd, "ver") || !strcmp(cmd, "about")) {
            print_string("TuxOS version 0.2.1 (Early)\n");
            print_string("Made by PSPGuyCVM\n");
            print_string("With love and no GRUB\n");
        } else if (!strcmp(cmd, "tux")) {
            print_string(
                "   .--.\n"
                "  |o_o |\n"
                "  |:_/ |\n"
                " //   \\ \\\n"
                "(|     | )\n"
                "/'\\_   _/`\\\n"
                "\\___)=(___/\n"
            );
        } else if (!strcmp(cmd, "shutdown")) {
            print_string("Powering off...\n");
            outw(0x604, 0x2000);
            asm volatile ("cli; hlt");
        } else if (!strcmp(cmd, "reboot")) {
            outb(0x64, 0xFE);
        } else if (!strcmp(cmd, "calc")) {
            int result = calc_expression(args);
            char buf[32];
            int_to_str(result, buf);
            print_string(buf);
            print_string("\n");
        } else if (!strcmp(cmd, "hex")) {
            int num = 0;
            const char *p = args;
            while (*p >= '0' && *p <= '9') {
                num = num * 10 + (*p - '0');
                p++;
            }
            print_string("0x");
            print_hex(num);
            print_string("\n");
        } else if (!strcmp(cmd, "random")) {
            unsigned int tsc;
            asm volatile ("rdtsc" : "=A" (tsc));
            print_string("0x");
            print_hex(tsc);
            print_string("\n");
        } else if (!strcmp(cmd, "ascii")) {
            print_ascii_table();
        } else if (!strcmp(cmd, "cowsay")) {
            cow_say(args);
        } else if (!strcmp(cmd, "fortune")) {
            print_fortune();
        } else if (!strcmp(cmd, "mem")) {
            print_string("Memory info not available.\n");
        } else if (!strcmp(cmd, "uptime")) {
            print_string("Uptime not available (no RTC yet).\n");
        } else if (!strcmp(cmd, "halt")) {
            kernel_panic("System halted.");
        } else if (!strcmp(cmd, "panic")) {
            kernel_panic("User requested kernel panic.");
        } else if (!strcmp(cmd, "pong")) {
            pong_game();
        } else if (!strcmp(cmd, "sl")) {
            print_sl();
        } else if (!strcmp(cmd, "nyancat")) {
            print_nyancat();
        } else if (!strcmp(cmd, "rickroll")) {
            print_rickroll();
        } else {
            print_string("Unknown command. Type 'help'.\n");
        }
    }
}

/* --- Kernel entry --- */
void kernel_main() {
    clear_screen();
    print_string("Welcome to TuxOS 0.2.1!\n");
    print_string("Version: Early 0.2ю\n");
    shell();
    while (1) {}
}
