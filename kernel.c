/* kernel.c - TuxOS 0.2 text-mode shell */

#define VIDEO_MEMORY 0xB8000
#define MAX_ROWS 25
#define MAX_COLS 80
#define WHITE_ON_BLACK 0x0F

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
    } while (num);
    if (sign) buf[i++] = '-';
    buf[i] = '\0';
    reverse_str(buf, i);
}

/* --- Keyboard with shift support --- */
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

void strcpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

/* Simple decimal to hex conversion */
void print_hex(unsigned int num) {
    char buf[16];
    int i = 0;
    do {
        int digit = num % 16;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        num /= 16;
    } while (num > 0);
    buf[i] = '\0';
    /* reverse it */
    for (int j = 0; j < i/2; j++) {
        char t = buf[j];
        buf[j] = buf[i-1-j];
        buf[i-1-j] = t;
    }
    print_string(buf);
}

/* --- New commands supporting functions --- */

/* Simple integer arithmetic parser: expects "<number><op><number>" no spaces */
int calc_expression(const char *expr) {
    int num1 = 0, num2 = 0;
    char op = 0;
    int i = 0;

    // skip leading spaces
    while (expr[i] == ' ') i++;
    // read first number
    while (expr[i] >= '0' && expr[i] <= '9') {
        num1 = num1 * 10 + (expr[i] - '0');
        i++;
    }
    // read operator
    if (expr[i] == '+' || expr[i] == '-' || expr[i] == '*' || expr[i] == '/') {
        op = expr[i];
        i++;
    } else {
        return 0; // invalid
    }
    // read second number
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

/* ASCII table printer */
void print_ascii_table() {
    for (int i = 32; i < 127; i += 8) {
        for (int j = 0; j < 8 && i+j < 127; j++) {
            char buf[8];
            buf[0] = i+j;
            buf[1] = ' ';
            buf[2] = '\0';
            print_string(buf);
        }
        print_string("\n");
    }
}

/* Cow ASCII art */
void cow_say(const char *msg) {
    int len = strlen(msg);
    print_string(" ");
    for (int i = 0; i < len + 2; i++) print_char('_');
    print_string("\n< ");
    print_string(msg);
    print_string(" >\n ");
    for (int i = 0; i < len + 2; i++) print_char('-');
    print_string("\n        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||\n");
}

/* Fortune messages */
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
    /* Not truly random – but we'll use a counter */
    static int counter = 0;
    int idx = counter++ % n;
    print_string(fortunes[idx]);
    print_string("\n");
}

/* --- Shell --- */
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
            print_string("Available commands:\n");
            print_string("help, whoami, echo, clear, uname, date, ls, pwd, ver, about, tux,\n");
            print_string("shutdown, reboot, calc <expr>, hex <num>, random, ascii,\n");
            print_string("cowsay <msg>, fortune, mem, uptime\n");
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
            print_string("TuxOS version 0.2 (Early)\n");
            print_string("Made by PSPGuy\n");
            print_string("Built with GCC + NASM, no GRUB\n");
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
            // convert decimal string to int manually
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
            print_string("Memory information not available (work in progress).\n");
        } else if (!strcmp(cmd, "uptime")) {
            print_string("Uptime not available (no RTC yet).\n");
        } else {
            print_string("Unknown command. Type 'help'.\n");
        }
    }
}

/* --- Kernel entry --- */
void kernel_main() {
    clear_screen();
    print_string("Welcome to TuxOS 0.2!\n");
    print_string("Version: Early 0.2.\n");
    shell();
    while (1) {}
}
