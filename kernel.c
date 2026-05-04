/* kernel.c - TuxOS 0.2.2 – piece of dog shit */

#define VIDEO_MEMORY 0xB8000
#define MAX_ROWS 25
#define MAX_COLS 80
#define WHITE_ON_BLACK 0x0F
#define BLUE_BG       0x1F
#define GREEN_ON_BLACK 0x0A

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

static int cursor_row = 0;
static int cursor_col = 0;
static unsigned int boot_epoch = 0;          /* RTC seconds at boot */
static unsigned int random_seed = 0;

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
        char *p = (char *)(VIDEO_MEMORY + 2*i);
        *p = ' '; *(p+1) = WHITE_ON_BLACK;
    }
    cursor_row = cursor_col = 0;
}
static void scroll_up() {
    for (int r = 1; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++) {
            char *dst = get_video_ptr(r-1,c), *src = get_video_ptr(r,c);
            *dst = *src; *(dst+1) = *(src+1);
        }
    for (int c = 0; c < MAX_COLS; c++) {
        char *p = get_video_ptr(MAX_ROWS-1,c);
        *p = ' '; *(p+1) = WHITE_ON_BLACK;
    }
}
void print_char(char c) {
    if (c == '\n') { cursor_col=0; cursor_row++; }
    else if (c == '\r') { cursor_col=0; }
    else {
        char *p = get_video_ptr(cursor_row, cursor_col);
        *p = c; *(p+1) = WHITE_ON_BLACK;
        cursor_col++;
    }
    if (cursor_col >= MAX_COLS) { cursor_col=0; cursor_row++; }
    if (cursor_row >= MAX_ROWS) { cursor_row=MAX_ROWS-1; scroll_up(); }
    unsigned short pos = cursor_row*MAX_COLS + cursor_col;
    outb(0x3D4,14); outb(0x3D5,(pos>>8)&0xFF);
    outb(0x3D4,15); outb(0x3D5,pos&0xFF);
}
void print_string(const char *str) {
    while (*str) print_char(*str++);
}

/* --- Integer to string --- */
static void reverse_str(char *s, int len) {
    for (int i=0; i<len/2; i++) { char t=s[i]; s[i]=s[len-1-i]; s[len-1-i]=t; }
}
void int_to_str(int num, char *buf) {
    int i=0, sign=0;
    if (num<0) { sign=1; num=-num; }
    do { buf[i++] = '0' + (num%10); num/=10; } while (num>0);
    if (sign) buf[i++] = '-';
    buf[i] = '\0';
    reverse_str(buf,i);
}

/* --- CMOS / RTC --- */
static int cmos_read(unsigned char reg) {
    outb(0x70, (1<<7) | reg);   /* NMI disable + register index */
    for (volatile int i=0; i<1000; i++);  /* tiny delay */
    return inb(0x71);
}
static unsigned char bcd_to_bin(unsigned char bcd) {
    return ((bcd>>4)*10) + (bcd & 0x0F);
}
void read_rtc(unsigned char *hour, unsigned char *min, unsigned char *sec,
              unsigned char *day, unsigned char *month, unsigned char *year) {
    /* wait until update not in progress */
    while (cmos_read(0x0A) & 0x80);
    *sec   = bcd_to_bin(cmos_read(0x00));
    *min   = bcd_to_bin(cmos_read(0x02));
    *hour  = bcd_to_bin(cmos_read(0x04));
    *day   = bcd_to_bin(cmos_read(0x07));
    *month = bcd_to_bin(cmos_read(0x08));
    *year  = bcd_to_bin(cmos_read(0x09));
}
unsigned int rtc_to_epoch(unsigned char h, unsigned char m, unsigned char s,
                          unsigned char d, unsigned char mo, unsigned char y) {
    /* approximate: month*days... not accurate but good for uptime */
    unsigned int days = (y*365) + (mo*30) + d;
    return (days*86400) + (h*3600) + (m*60) + s;
}
unsigned int get_rtc_epoch() {
    unsigned char h,m,s,d,mo,y;
    read_rtc(&h,&m,&s,&d,&mo,&y);
    return rtc_to_epoch(h,m,s,d,mo,y);
}

/* --- Random (LCG) --- */
static void srand(unsigned int seed) { random_seed = seed; }
static int rand() {
    random_seed = (1103515245*random_seed + 12345) & 0x7fffffff;
    return random_seed;
}
int rand_range(int min, int max) {
    if (max <= min) return min;
    return min + (rand() % (max - min + 1));
}

/* --- Kernel panic (unchanged) --- */
void kernel_panic(const char *reason) {
    for (int i=0; i<MAX_ROWS*MAX_COLS; i++) {
        char *p = (char*)(VIDEO_MEMORY+2*i);
        *p = ' '; *(p+1)=BLUE_BG;
    }
    const char *msg = "KERNEL PANIC :(";
    int len=0; while(msg[len]) len++;
    int row=10, col=(MAX_COLS-len)/2;
    for (int i=0; i<len; i++) {
        char *p = get_video_ptr(row,col+i);
        *p=msg[i]; *(p+1)=BLUE_BG;
    }
    int rlen=0; while(reason[rlen]) rlen++;
    row=23; col=(MAX_COLS-rlen)/2;
    for (int i=0; i<rlen; i++) {
        char *p = get_video_ptr(row,col+i);
        *p=reason[i]; *(p+1)=BLUE_BG;
    }
    asm volatile ("cli; hlt");
    while(1);
}

/* --- Keyboard input with shift (unchanged) --- */
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
    int pos=0, shift=0;
    while (1) {
        while (!(inb(KEYBOARD_STATUS_PORT)&0x01));
        unsigned char sc = inb(KEYBOARD_DATA_PORT);
        if (sc & 0x80) { sc &= 0x7F; if (sc==0x2A||sc==0x36) shift=0; continue; }
        if (sc==0x2A || sc==0x36) { shift=1; continue; }
        if (sc==0x0E) {
            if (pos>0 && cursor_col>0) {
                pos--; cursor_col--;
                char *p = get_video_ptr(cursor_row,cursor_col);
                *p=' '; *(p+1)=WHITE_ON_BLACK;
                unsigned short cur = cursor_row*MAX_COLS+cursor_col;
                outb(0x3D4,14); outb(0x3D5,(cur>>8)&0xFF);
                outb(0x3D4,15); outb(0x3D5,cur&0xFF);
            }
        } else if (sc==0x1C) {
            print_char('\n');
            buffer[pos]='\0';
            return pos;
        } else if (sc < sizeof(scancode_lower)) {
            char ascii = shift ? scancode_upper[sc] : scancode_lower[sc];
            if (ascii && pos<max-1) { buffer[pos++]=ascii; print_char(ascii); }
        }
    }
}

/* --- Non‑blocking keyboard for games (unchanged) --- */
static int kbhit() { return inb(KEYBOARD_STATUS_PORT) & 0x01; }
static int get_scancode() { if (kbhit()) return inb(KEYBOARD_DATA_PORT); return -1; }

/* --- String utilities --- */
int strcmp(const char *a, const char *b) {
    while (*a && *a==*b) { a++; b++; }
    return *a - *b;
}
int strlen(const char *s) { int l=0; while(*s++) l++; return l; }
void strcpy(char *dst, const char *src) { while(*src) *dst++ = *src++; *dst=0; }

/* --- Print hex (unchanged) --- */
void print_hex(unsigned int num) {
    char buf[16]; int i=0;
    if (!num) { print_string("0"); return; }
    do { int d=num%16; buf[i++]=(d<10)?('0'+d):('A'+d-10); num/=16; } while(num);
    buf[i]=0;
    for (int j=0; j<i/2; j++) { char t=buf[j]; buf[j]=buf[i-1-j]; buf[i-1-j]=t; }
    print_string(buf);
}

/* ===========================================================
   GUESS THE NUMBER
   =========================================================== */
void guess_game() {
    int number = rand_range(1, 100);
    int guess, attempts = 0;
    char input[32];
    clear_screen();
    print_string("I'm thinking of a number between 1 and 100.\n");
    while (1) {
        print_string("Your guess: ");
        int len = read_line(input, sizeof(input));
        if (len==0) continue;
        guess = 0;
        for (int i=0; input[i]; i++) guess = guess*10 + (input[i]-'0');
        attempts++;
        if (guess < number) print_string("Too low!\n");
        else if (guess > number) print_string("Too high!\n");
        else {
            print_string("Correct! You got it in ");
            char buf[8]; int_to_str(attempts, buf);
            print_string(buf); print_string(" tries.\n");
            break;
        }
    }
    print_string("Press any key...");
    while (!kbhit());
    while (kbhit()) get_scancode();
    clear_screen();
}

/* ===========================================================
   FAKE SYSTEM COMMANDS
   =========================================================== */
void fake_ps() {
    print_string("PID  USER   TIME   COMMAND\n");
    print_string("  1  root   0:00   init [TuxOS]\n");
    print_string("  2  root   0:00   kshell\n");
    print_string("  3  root   0:01   idle\n");
}
void fake_top() {
    clear_screen();
    print_string("top - 12:34:56 up 0 days,  0:05,  1 user\n");
    print_string("Tasks:   3 total,   1 running,   2 sleeping\n");
    print_string("Mem:    64M total,   12M used,   52M free\n\n");
    fake_ps();
    print_string("\nPress any key to exit...");
    while (!kbhit());
    while (kbhit()) get_scancode();
    clear_screen();
}
void fake_kill(const char *args) {
    int pid = 0;
    for (int i=0; args[i]; i++) pid = pid*10 + (args[i]-'0');
    if (pid==0) { print_string("Usage: kill <pid>\n"); return; }
    print_string("kill: process "); char buf[8]; int_to_str(pid,buf); print_string(buf);
    print_string(" not found (but pretend it worked).\n");
}
void fake_dmesg() {
    print_string("[ 0.00] TuxOS 0.3 booting...\n");
    print_string("[ 0.02] VGA text mode 80x25 activated\n");
    print_string("[ 0.10] PS/2 keyboard detected\n");
    print_string("[ 0.15] Shell started\n");
}
void fake_who() {
    print_string("root     tty1   May 4 12:34\n");
}

/* --- Jokes (unchanged) --- */
void print_sl() {
    print_string("             (@@@)\n    (------)   |   \n   (_______)| -+- |\n   /       /  |   |\n  oooooooo oooo   oo\nchuff chuff chuff...\n");
}
void print_nyancat() {
    print_string("+      o     +              o\n    +             +     +    \n        ,~~~.          \n       (  o o)~~~~~~~~~ \n        \\_/  \\         \n        /   \\  \\       \n       /~~~~~\\  \\      \n+     o       +    o    +\nNyan nyan nyan...\n");
}
void print_rickroll() {
    print_string("Never gonna give you up\nNever gonna let you down\nNever gonna run around and desert you\nNever gonna make you cry\nNever gonna say goodbye\nNever gonna tell a lie and hurt you\n");
}
void print_fortune() {
    const char *fortunes[] = {
        "You will write a cool OS.",
        "Bug is just a feature waiting to be found.",
        "Tux is silently judging your code.",
        "Real programmers use butterflies.",
        "Segmentation fault (core dumped) ... just kidding.",
        "The answer is 42.",
        "Don't panic!",
        "All your base are belong to us."
    };
    int n = sizeof(fortunes)/sizeof(fortunes[0]);
    print_string(fortunes[rand()%n]); print_string("\n");
}
void cow_say(const char *msg) {
    int len = strlen(msg);
    print_char(' ');
    for (int i=0; i<len+2; i++) print_char('_');
    print_string("\n< "); print_string(msg); print_string(" >\n ");
    for (int i=0; i<len+2; i++) print_char('-');
    print_string("\n        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||\n");
}
void print_ascii_table() {
    for (int i=32; i<127; i+=8) {
        for (int j=0; j<8 && i+j<127; j++) { char buf[8]={i+j,' ',0}; print_string(buf); }
        print_string("\n");
    }
}

/* --- Calculator (unchanged) --- */
int calc_expression(const char *expr) {
    int num1=0, num2=0, i=0; char op=0;
    while (expr[i]==' ') i++;
    while (expr[i]>='0' && expr[i]<='9') { num1 = num1*10 + (expr[i]-'0'); i++; }
    if (expr[i]=='+'||expr[i]=='-'||expr[i]=='*'||expr[i]=='/') { op=expr[i]; i++; }
    else return 0;
    while (expr[i]>='0' && expr[i]<='9') { num2 = num2*10 + (expr[i]-'0'); i++; }
    switch(op) {
        case '+': return num1+num2;
        case '-': return num1-num2;
        case '*': return num1*num2;
        case '/': if (num2) return num1/num2; else return 0;
    }
    return 0;
}

/* ===========================================================
   COMMAND SHELL (massive!)
   =========================================================== */
void shell() {
    char input[128];
    while (1) {
        print_string("TuxOS> ");
        int len = read_line(input, sizeof(input));
        if (len==0) continue;
        char *cmd = input, *args = "";
        for (int i=0; i<len; i++) if (input[i]==' ') { input[i]=0; args=input+i+1; break; }

        /* --- Core system --- */
        if (!strcmp(cmd,"help")) {
            print_string("Core: help, whoami, echo, clear, uname, date, ls, pwd, ver, about, tux\n");
            print_string("System: shutdown, reboot, halt, panic, ps, top, kill, dmesg, who, free, uptime\n");
            print_string("Utils: calc, hex, random, ascii, strrev, strlen, cowsay, fortune\n");
            print_string("Games: pong, guess\n");
            print_string("Fun: sl, nyancat, rickroll\n");
        }
        else if (!strcmp(cmd,"whoami")) print_string("root\n");
        else if (!strcmp(cmd,"echo")) { print_string(args); print_string("\n"); }
        else if (!strcmp(cmd,"clear")) clear_screen();
        else if (!strcmp(cmd,"uname")) { print_string("TuxOS 0.3 (x86)\n"); } /* extended */
        else if (!strcmp(cmd,"date")) {
            unsigned char h,m,s,d,mo,y;
            read_rtc(&h,&m,&s,&d,&mo,&y);
            char buf[64];
            int_to_str(2000+y, buf); print_string(buf); print_char('-');
            if (mo<10) print_char('0'); int_to_str(mo,buf); print_string(buf); print_char('-');
            if (d<10) print_char('0'); int_to_str(d,buf); print_string(buf); print_char(' ');
            if (h<10) print_char('0'); int_to_str(h,buf); print_string(buf); print_char(':');
            if (m<10) print_char('0'); int_to_str(m,buf); print_string(buf); print_char(':');
            if (s<10) print_char('0'); int_to_str(s,buf); print_string(buf); print_char('\n');
        }
        else if (!strcmp(cmd,"ls")) print_string("No filesystem.\n");
        else if (!strcmp(cmd,"pwd")) print_string("/\n");
        else if (!strcmp(cmd,"ver")||!strcmp(cmd,"about")) {
            print_string("TuxOS version 0.3 (Early)\nMade by PSPGuyCVM\nNo GRUB, pure ASM + C\n");
        }
        else if (!strcmp(cmd,"tux")) print_string("   .--.\n  |o_o |\n  |:_/ |\n //   \\ \\\n(|     | )\n/'\\_   _/`\\\n\\___)=(___/\n");
        else if (!strcmp(cmd,"shutdown")) { outw(0x604,0x2000); asm("cli; hlt"); }
        else if (!strcmp(cmd,"reboot")) { outb(0x64,0xFE); }
        else if (!strcmp(cmd,"halt")) kernel_panic("System halted.");
        else if (!strcmp(cmd,"panic")) kernel_panic(args);

        /* --- System commands --- */
        else if (!strcmp(cmd,"ps")) fake_ps();
        else if (!strcmp(cmd,"top")) fake_top();
        else if (!strcmp(cmd,"kill")) fake_kill(args);
        else if (!strcmp(cmd,"dmesg")) fake_dmesg();
        else if (!strcmp(cmd,"who")) fake_who();
        else if (!strcmp(cmd,"free")) print_string("Total: 64 MB  Free: 52 MB  Used: 12 MB\n");
        else if (!strcmp(cmd,"uptime")) {
            unsigned int now = get_rtc_epoch();
            unsigned int diff = now - boot_epoch;
            unsigned int d = diff/86400, h = (diff%86400)/3600, m = (diff%3600)/60;
            char buf[8];
            print_string("up "); if(d) { int_to_str(d,buf); print_string(buf); print_string(" days, "); }
            if(h<10) print_char('0'); int_to_str(h,buf); print_string(buf); print_char(':');
            if(m<10) print_char('0'); int_to_str(m,buf); print_string(buf); print_char('\n');
        }

        /* --- Utils --- */
        else if (!strcmp(cmd,"calc")) { int res=calc_expression(args); char buf[16]; int_to_str(res,buf); print_string(buf); print_string("\n"); }
        else if (!strcmp(cmd,"hex")) {
            int num=0; const char *p=args; while (*p>='0'&&*p<='9') { num=num*10+(*p-'0'); p++; }
            print_string("0x"); print_hex(num); print_string("\n");
        }
        else if (!strcmp(cmd,"random")) { print_string("0x"); print_hex(rand()); print_string("\n"); }
        else if (!strcmp(cmd,"ascii")) print_ascii_table();
        else if (!strcmp(cmd,"strrev")) { char rev[128]; int l=strlen(args); for (int i=0;i<l;i++) rev[i]=args[l-1-i]; rev[l]=0; print_string(rev); print_string("\n"); }
        else if (!strcmp(cmd,"strlen")) { char buf[8]; int_to_str(strlen(args),buf); print_string(buf); print_string("\n"); }
        else if (!strcmp(cmd,"cowsay")) cow_say(args);
        else if (!strcmp(cmd,"fortune")) print_fortune();

        /* --- Games --- */
        else if (!strcmp(cmd,"pong")) { /* reuse old pong (still present) */ extern void pong_game(); pong_game(); }
        else if (!strcmp(cmd,"guess")) guess_game();

        /* --- Fun --- */
        else if (!strcmp(cmd,"sl")) print_sl();
        else if (!strcmp(cmd,"nyancat")) print_nyancat();
        else if (!strcmp(cmd,"rickroll")) print_rickroll();

        else { print_string("Unknown. Try 'help'.\n"); }
    }
}

/* ===========================================================
   PONG GAME (restored from TuxOS 0.2.1, playable speed)
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
        *top = '-'; *(top+1) = 0x0F;
        char *bot = get_video_ptr(MAX_ROWS-1, c);
        *bot = '-'; *(bot+1) = 0x0F;
    }
    for (int r = 1; r < MAX_ROWS-1; r++) {
        char *left = get_video_ptr(r, 0);
        *left = '|'; *(left+1) = 0x0F;
        char *right = get_video_ptr(r, MAX_COLS-1);
        *right = '|'; *(right+1) = 0x0F;
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
        /* PLAYABLE SPEED – adjust this number if needed */
        for (volatile int i = 0; i < 20000000; i++);

        /* read keyboard */
        int sc;
        while ((sc = get_scancode()) != -1) {
            if (sc & 0x80) continue;
            if (sc == 0x10) {          /* Q to quit */
                clear_screen();
                return;
            }
            /* left paddle: W / S */
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
            /* right paddle: Up / Down arrows */
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
                *get_video_ptr(0, c) = '-'; *(get_video_ptr(0, c)+1) = 0x0F;
                *get_video_ptr(MAX_ROWS-1, c) = '-'; *(get_video_ptr(MAX_ROWS-1, c)+1) = 0x0F;
            }
            for (int r = 1; r < MAX_ROWS-1; r++) {
                *get_video_ptr(r, 0) = '|'; *(get_video_ptr(r, 0)+1) = 0x0F;
                *get_video_ptr(r, MAX_COLS-1) = '|'; *(get_video_ptr(r, MAX_COLS-1)+1) = 0x0F;
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
                *get_video_ptr(0, c) = '-'; *(get_video_ptr(0, c)+1) = 0x0F;
                *get_video_ptr(MAX_ROWS-1, c) = '-'; *(get_video_ptr(MAX_ROWS-1, c)+1) = 0x0F;
            }
            for (int r = 1; r < MAX_ROWS-1; r++) {
                *get_video_ptr(r, 0) = '|'; *(get_video_ptr(r, 0)+1) = 0x0F;
                *get_video_ptr(r, MAX_COLS-1) = '|'; *(get_video_ptr(r, MAX_COLS-1)+1) = 0x0F;
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

/* --- Kernel entry --- */
void kernel_main() {
    clear_screen();
    /* seed random */
    unsigned int seed;
    asm volatile ("rdtsc" : "=A"(seed));
    srand(seed);
    boot_epoch = get_rtc_epoch();   /* for uptime */
    print_string("Welcome to TuxOS 0.2.2!\n");
    print_string("Version: Early 0.2.2, not for public.\n");
    shell();
    while(1){}
}
