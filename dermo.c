#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/types.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <pty.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define _POSIX_C_SOURCE 200809L

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define COLS (WINDOW_WIDTH / FONT_WIDTH)
#define ROWS (WINDOW_HEIGHT / FONT_HEIGHT)
#define BUFFER_SIZE 4096

typedef struct {
    Display *display;
    Window window;
    GC gc;
    XFontStruct *font;
    int screen;
    unsigned long black, white, green, red, blue, yellow;
    int master_fd;
    pid_t child_pid;
    char buffer[ROWS][COLS];
    int cursor_x, cursor_y;
    int scroll_top;
} Terminal;

Terminal term;

void init_colors() {
    XColor color;
    Colormap colormap = DefaultColormap(term.display, term.screen);
    
    term.black = BlackPixel(term.display, term.screen);
    term.white = WhitePixel(term.display, term.screen);
    
    // Green
    XParseColor(term.display, colormap, "#00FF00", &color);
    XAllocColor(term.display, colormap, &color);
    term.green = color.pixel;
    
    // Red
    XParseColor(term.display, colormap, "#FF0000", &color);
    XAllocColor(term.display, colormap, &color);
    term.red = color.pixel;
    
    // Blue
    XParseColor(term.display, colormap, "#0000FF", &color);
    XAllocColor(term.display, colormap, &color);
    term.blue = color.pixel;
    
    // Yellow
    XParseColor(term.display, colormap, "#FFFF00", &color);
    XAllocColor(term.display, colormap, &color);
    term.yellow = color.pixel;
}

void init_x11() {
    term.display = XOpenDisplay(NULL);
    if (!term.display) {
        fprintf(stderr, "Не удается открыть X display\n");
        exit(1);
    }
    
    term.screen = DefaultScreen(term.display);
    term.window = XCreateSimpleWindow(term.display, 
                                     RootWindow(term.display, term.screen),
                                     100, 100, WINDOW_WIDTH, WINDOW_HEIGHT,
                                     1, term.black, term.black);
    
    XSelectInput(term.display, term.window, 
                ExposureMask | KeyPressMask | StructureNotifyMask);
    
    term.gc = XCreateGC(term.display, term.window, 0, NULL);
    
    // Загрузка шрифта
    term.font = XLoadQueryFont(term.display, "fixed");
    if (!term.font) {
        term.font = XLoadQueryFont(term.display, "*");
    }
    
    if (term.font) {
        XSetFont(term.display, term.gc, term.font->fid);
    }
    
    init_colors();
    XSetForeground(term.display, term.gc, term.green);
    XSetBackground(term.display, term.gc, term.black);
    
    XStoreName(term.display, term.window, "Dermux Terminal");
    XMapWindow(term.display, term.window);
}

void clear_buffer() {
    for (int i = 0; i < ROWS; i++) {
        memset(term.buffer[i], 0, COLS);
    }
    term.cursor_x = 0;
    term.cursor_y = 0;
    term.scroll_top = 0;
}

void scroll_up() {
    for (int i = 0; i < ROWS - 1; i++) {
        memcpy(term.buffer[i], term.buffer[i + 1], COLS);
    }
    memset(term.buffer[ROWS - 1], 0, COLS);
    if (term.cursor_y > 0) {
        term.cursor_y--;
    }
}

void put_char(char c) {
    if (c == '\n') {
        term.cursor_x = 0;
        term.cursor_y++;
        if (term.cursor_y >= ROWS) {
            scroll_up();
        }
    } else if (c == '\r') {
        term.cursor_x = 0;
    } else if (c == '\b') {
        if (term.cursor_x > 0) {
            term.cursor_x--;
            term.buffer[term.cursor_y][term.cursor_x] = ' ';
        }
    } else if (c >= 32 && c <= 126) { // Печатные символы
        if (term.cursor_x >= COLS) {
            term.cursor_x = 0;
            term.cursor_y++;
            if (term.cursor_y >= ROWS) {
                scroll_up();
            }
        }
        term.buffer[term.cursor_y][term.cursor_x] = c;
        term.cursor_x++;
    }
}

void draw_terminal() {
    XClearWindow(term.display, term.window);
    
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (term.buffer[y][x] != 0) {
                char str[2] = {term.buffer[y][x], 0};
                XDrawString(term.display, term.window, term.gc,
                           x * FONT_WIDTH, (y + 1) * FONT_HEIGHT, str, 1);
            }
        }
    }
    
    // Рисуем курсор
    XFillRectangle(term.display, term.window, term.gc,
                   term.cursor_x * FONT_WIDTH,
                   term.cursor_y * FONT_HEIGHT + 2,
                   FONT_WIDTH, 2);
    
    XFlush(term.display);
}

int spawn_shell() {
    struct winsize ws = {ROWS, COLS, WINDOW_WIDTH, WINDOW_HEIGHT};
    
    term.child_pid = forkpty(&term.master_fd, NULL, NULL, &ws);
    
    if (term.child_pid < 0) {
        perror("forkpty");
        return -1;
    }
    
    if (term.child_pid == 0) {
        // Дочерний процесс - запускаем shell
        char *shell = getenv("SHELL");
        if (!shell) shell = "/bin/bash";
        
        execl(shell, shell, NULL);
        perror("exec");
        exit(1);
    }
    
    // Устанавливаем неблокирующий режим
    fcntl(term.master_fd, F_SETFL, O_NONBLOCK);
    
    return 0;
}

void handle_key(XKeyEvent *event) {
    char buffer[32];
    KeySym keysym;
    int len;
    
    len = XLookupString(event, buffer, sizeof(buffer), &keysym, NULL);
    
    if (len > 0) {
        // Отправляем ввод в shell
        write(term.master_fd, buffer, len);
    } else {
        // Обработка специальных клавиш
        switch (keysym) {
            case XK_Return:
                write(term.master_fd, "\r", 1);
                break;
            case XK_BackSpace:
                write(term.master_fd, "\b", 1);
                break;
            case XK_Tab:
                write(term.master_fd, "\t", 1);
                break;
            case XK_Up:
                write(term.master_fd, "\033[A", 3);
                break;
            case XK_Down:
                write(term.master_fd, "\033[B", 3);
                break;
            case XK_Right:
                write(term.master_fd, "\033[C", 3);
                break;
            case XK_Left:
                write(term.master_fd, "\033[D", 3);
                break;
            case XK_Delete:
                write(term.master_fd, "\033[3~", 4);
                break;
        }
    }
}

void read_from_shell() {
    char buffer[BUFFER_SIZE];
    ssize_t len;
    
    while ((len = read(term.master_fd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < len; i++) {
            put_char(buffer[i]);
        }
        draw_terminal();
    }
}

void cleanup() {
    if (term.child_pid > 0) {
        kill(term.child_pid, SIGTERM);
        waitpid(term.child_pid, NULL, 0);
    }
    
    if (term.master_fd >= 0) {
        close(term.master_fd);
    }
    
    if (term.display) {
        if (term.font) {
            XFreeFont(term.display, term.font);
        }
        XFreeGC(term.display, term.gc);
        XDestroyWindow(term.display, term.window);
        XCloseDisplay(term.display);
    }
}

void signal_handler(int sig) {
    (void)sig; // Убираем предупреждение
    cleanup();
    exit(0);
}

int main() {
    printf("Запускаю Dermux Terminal...\n");
    
    // Устанавливаем обработчики сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGCHLD, SIG_IGN);
    
    // Инициализация
    memset(&term, 0, sizeof(term));
    term.master_fd = -1;
    
    init_x11();
    clear_buffer();
    
    if (spawn_shell() < 0) {
        cleanup();
        return 1;
    }
    
    printf("Dermux готов к работе!\n");
    
    // Главный цикл событий
    XEvent event;
    fd_set fds;
    int x11_fd = ConnectionNumber(term.display);
    
    while (1) {
        FD_ZERO(&fds);
        FD_SET(x11_fd, &fds);
        FD_SET(term.master_fd, &fds);
        
        int max_fd = (x11_fd > term.master_fd) ? x11_fd : term.master_fd;
        
        if (select(max_fd + 1, &fds, NULL, NULL, NULL) > 0) {
            if (FD_ISSET(x11_fd, &fds)) {
                while (XPending(term.display)) {
                    XNextEvent(term.display, &event);
                    
                    switch (event.type) {
                        case Expose:
                            draw_terminal();
                            break;
                            
                        case KeyPress:
                            handle_key(&event.xkey);
                            break;
                            
                        case ClientMessage:
                        case DestroyNotify:
                            cleanup();
                            return 0;
                    }
                }
            }
            
            if (FD_ISSET(term.master_fd, &fds)) {
                read_from_shell();
            }
        }
        
        // Проверяем, жив ли дочерний процесс
        int status;
        if (waitpid(term.child_pid, &status, WNOHANG) > 0) {
            printf("Shell завершился\n");
            break;
        }
    }
    
    cleanup();
    return 0;
}