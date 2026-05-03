/* TuxOS - a minimal operating system
 * Copyright (C) 2025  Your Name
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * ...
 */

#define VIDEO_MEMORY 0xB8000
#define MAX_ROWS 25
#define MAX_COLS 80
#define WHITE_ON_BLACK 0x0F

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

static int cursor_row = 0;
static int cursor_col = 0;

/* --- Low‑level I/O --- */
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

/* --- VGA helpers --- */
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
    /* Move rows up */
    for (int r = 1; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            char *dst = get_video_ptr(r - 1, c);
            char *src = get_video_ptr(r, c);
            *dst = *src;
            *(dst + 1) = *(src + 1);
        }
    }
    /* Clear last row */
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

    /* Update hardware cursor */
    unsigned short pos = cursor_row * MAX_COLS + cursor_col;
    outb(0x3D4, 14);
    outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 15);
    outb(0x3D5, pos & 0xFF);
}

void print_string(const char *str) {
    while (*str) print_char(*str++);
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

/* --- Power off (QEMU/Bochs/VirtualBox) --- */
void power_off() {
    outw(0x604, 0x2000);
    asm volatile ("cli; hlt");
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
            print_string("help, whoami, echo, clear, uname, date, ls, pwd, ver, tux, shutdown, reboot\n");
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
            print_string("Sun May  3 12:00:00 UTC 2026\n");
        } else if (!strcmp(cmd, "ls")) {
            print_string("No filesystem.\n");
        } else if (!strcmp(cmd, "pwd")) {
            print_string("/\n");
        } else if (!strcmp(cmd, "ver")) {
            print_string("TuxOS version Early 0.1, not for public.\n");
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
            power_off();
        } else if (!strcmp(cmd, "reboot")) {
            outb(0x64, 0xFE);
        } else {
            print_string("Unknown command. Type 'help'.\n");
        }
    }
}

/* --- Kernel entry --- */
void kernel_main() {
    clear_screen();
    print_string("Welcome to TuxOS!\n");
    print_string("Version: Early 0.1, not for public.\n");
    shell();
    while (1) {}
}
