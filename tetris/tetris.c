/*
 * TETRIS en C (consola)
 * Compilar:  gcc tetris.c -o tetris
 * Controles: A/D o flechas = mover, W o flecha arriba = rotar,
 *            S o flecha abajo = bajar, ESPACIO = caída rápida, Q = salir
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- Capa de terminal (Windows vs Linux/macOS) ---------- */
#ifdef _WIN32
#include <conio.h>
#include <windows.h>

static void sleep_ms(int ms) { Sleep(ms); }

static void cursor_home(void) {
    COORD c = {0, 0};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

static void set_cursor_visible(BOOL v) {
    CONSOLE_CURSOR_INFO info = {1, v};
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
}

static void term_init(void) { system("cls"); set_cursor_visible(FALSE); }
static void term_restore(void) { set_cursor_visible(TRUE); }

static int read_key(void) {
    if (!_kbhit()) return -1;
    int c = _getch();
    if (c == 0 || c == 224) {            /* teclas especiales (flechas) */
        c = _getch();
        switch (c) {
            case 72: return 'w';
            case 80: return 's';
            case 75: return 'a';
            case 77: return 'd';
        }
        return -1;
    }
    return c;
}
#else
#include <termios.h>
#include <unistd.h>

static struct termios orig_term;

static void sleep_ms(int ms) { usleep(ms * 1000); }
static void cursor_home(void) { printf("\033[H"); }

static void term_restore(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
    printf("\033[?25h");
    fflush(stdout);
}

static void term_init(void) {
    struct termios t;
    tcgetattr(STDIN_FILENO, &orig_term);
    t = orig_term;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;                    /* read() no bloquea */
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    printf("\033[2J\033[?25l");
}

static int read_key(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) <= 0) return -1;
    if (c == 27) {                       /* secuencia de escape: flechas */
        unsigned char s[2];
        if (read(STDIN_FILENO, s, 2) == 2 && s[0] == '[') {
            switch (s[1]) {
                case 'A': return 'w';
                case 'B': return 's';
                case 'C': return 'd';
                case 'D': return 'a';
            }
        }
        return -1;
    }
    return c;
}
#endif

/* ---------- Juego ---------- */
#define W 10
#define H 20

typedef struct {
    int shape[4][4];
    int n;          /* tamaño de la matriz de la pieza (2, 3 o 4) */
    int x, y;       /* posición de la esquina superior izquierda */
    int type;
} Piece;

static const int   sizes[7]    = {4, 2, 3, 3, 3, 3, 3};
static const char *shapes[7][4] = {
    {"....", "XXXX", "....", "...."},   /* I */
    {"XX",   "XX"},                     /* O */
    {".X.",  "XXX", "..."},             /* T */
    {".XX",  "XX.", "..."},             /* S */
    {"XX.",  ".XX", "..."},             /* Z */
    {"X..",  "XXX", "..."},             /* J */
    {"..X",  "XXX", "..."}              /* L */
};

static int board[H][W];                 /* 0 = vacío, 1..7 = pieza fija */

static void make_piece(Piece *p, int type) {
    memset(p, 0, sizeof *p);
    p->type = type;
    p->n = sizes[type];
    for (int i = 0; i < p->n; i++)
        for (int j = 0; j < p->n; j++)
            p->shape[i][j] = (shapes[type][i][j] == 'X');
    p->x = W / 2 - p->n / 2;
    p->y = 0;
}

static void rotate(Piece *p) {          /* giro de 90° en sentido horario */
    int r[4][4] = {{0}};
    for (int i = 0; i < p->n; i++)
        for (int j = 0; j < p->n; j++)
            r[j][p->n - 1 - i] = p->shape[i][j];
    memcpy(p->shape, r, sizeof r);
}

static int collides(const Piece *p, int nx, int ny) {
    for (int i = 0; i < p->n; i++)
        for (int j = 0; j < p->n; j++) {
            if (!p->shape[i][j]) continue;
            int bx = nx + j, by = ny + i;
            if (bx < 0 || bx >= W || by >= H) return 1;
            if (by >= 0 && board[by][bx]) return 1;
        }
    return 0;
}

static void lock_piece(const Piece *p) {
    for (int i = 0; i < p->n; i++)
        for (int j = 0; j < p->n; j++)
            if (p->shape[i][j] && p->y + i >= 0)
                board[p->y + i][p->x + j] = p->type + 1;
}

static int clear_lines(void) {
    int cleared = 0;
    for (int i = H - 1; i >= 0; i--) {
        int full = 1;
        for (int j = 0; j < W; j++)
            if (!board[i][j]) { full = 0; break; }
        if (full) {
            for (int k = i; k > 0; k--) memcpy(board[k], board[k - 1], sizeof board[0]);
            memset(board[0], 0, sizeof board[0]);
            cleared++;
            i++;                         /* volver a revisar la misma fila */
        }
    }
    return cleared;
}

static void draw(const Piece *cur, const Piece *next, int score, int lines, int level) {
    int view[H][W];
    memcpy(view, board, sizeof view);
    for (int i = 0; i < cur->n; i++)
        for (int j = 0; j < cur->n; j++)
            if (cur->shape[i][j] && cur->y + i >= 0)
                view[cur->y + i][cur->x + j] = cur->type + 1;

    cursor_home();
    printf("  T E T R I S\n");
    for (int i = 0; i < H; i++) {
        printf(" <!");
        for (int j = 0; j < W; j++) printf(view[i][j] ? "[]" : " .");
        printf("!>");
        if (i == 1) printf("   Puntos: %-6d", score);
        if (i == 2) printf("   Lineas: %-6d", lines);
        if (i == 3) printf("   Nivel:  %-6d", level);
        if (i == 5) printf("   Siguiente:");
        if (i >= 6 && i <= 9) {
            printf("   ");
            for (int j = 0; j < 4; j++)
                printf((next->n > i - 6 && j < next->n && next->shape[i - 6][j]) ? "[]" : "  ");
        }
        printf("\n");
    }
    printf(" <!"); for (int j = 0; j < W; j++) printf("=="); printf("!>\n");
    printf("   \\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\n");
    printf(" A/D mover  W rotar  S bajar  ESPACIO caer  Q salir\n");
    fflush(stdout);
}

int main(void) {
    srand((unsigned)time(NULL));
    term_init();
    atexit(term_restore);

    Piece cur, next;
    make_piece(&cur, rand() % 7);
    make_piece(&next, rand() % 7);

    int score = 0, lines = 0, level = 1;
    int acc = 0;                         /* ms acumulados para la gravedad */
    int running = 1;

    while (running) {
        int interval = 600 - (level - 1) * 50;
        if (interval < 100) interval = 100;

        int k = read_key();
        switch (k) {
            case 'q': case 'Q': running = 0; break;
            case 'a': case 'A':
                if (!collides(&cur, cur.x - 1, cur.y)) cur.x--;
                break;
            case 'd': case 'D':
                if (!collides(&cur, cur.x + 1, cur.y)) cur.x++;
                break;
            case 's': case 'S':
                if (!collides(&cur, cur.x, cur.y + 1)) { cur.y++; score++; }
                break;
            case 'w': case 'W': {
                Piece t = cur;
                rotate(&t);
                static const int kicks[] = {0, -1, 1, -2, 2};   /* "wall kick" simple */
                for (int i = 0; i < 5; i++)
                    if (!collides(&t, t.x + kicks[i], t.y)) {
                        t.x += kicks[i];
                        cur = t;
                        break;
                    }
                break;
            }
            case ' ':
                while (!collides(&cur, cur.x, cur.y + 1)) { cur.y++; score += 2; }
                acc = interval;          /* fijar de inmediato */
                break;
        }

        acc += 16;
        if (acc >= interval) {
            acc = 0;
            if (!collides(&cur, cur.x, cur.y + 1)) {
                cur.y++;
            } else {
                lock_piece(&cur);
                int c = clear_lines();
                static const int pts[] = {0, 100, 300, 500, 800};
                score += pts[c] * level;
                lines += c;
                level = lines / 10 + 1;

                cur = next;
                make_piece(&next, rand() % 7);
                if (collides(&cur, cur.x, cur.y)) {   /* no cabe: fin del juego */
                    draw(&cur, &next, score, lines, level);
                    printf("\n  *** GAME OVER ***  Puntos finales: %d\n", score);
                    running = 0;
                }
            }
        }

        if (running) draw(&cur, &next, score, lines, level);
        sleep_ms(16);
    }
    return 0;
}