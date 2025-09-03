//TODO: check terminal has colors
//TODO: limit how much big files can be opened
//TODO: add bindings help page

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ncurses.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <math.h>

#define TAB_STOP 4
#define QUIT_TIMES 1

#define ctrl(k) ((k) & 0x1f)

typedef struct stack {
	int ch;
	int cx;
	int cy;
	int ins;
	struct stack *next;
}stack;

typedef struct undo_stack {
	stack *top;
}undo_stack;

typedef struct redo_stack {
	stack *top;
}redo_stack;

typedef struct erow {
	int size;
	int rsize;
	char *chars;
	char *render;
}erow;

struct editorConfig {
	int cx, cy;
	int rx;
	int rowoff;
	int coloff;
	int rows;
	int cols;
	int numrows;
	int line_width;
	erow *row;
	int dirty;
	char *filename;
	char statusmsg[100];
	time_t statusmsg_time;
	undo_stack u;
	redo_stack r;
};

struct editorConfig E;

typedef struct abuf {
	char *b;
	chtype *c;
	int len;
}abuf;

#define ABUF_INIT {NULL, NULL, 0}

void die(const char *s);

void abAppend(abuf *ab, const char *s, int len);

void abFree(abuf *ab);

int editorRowCxToRx(erow *row, int cx);

int editorRowRxToCx(erow *row, int rx);

void editorScroll();

void editorDrawRows(abuf *ab);

void editorDrawStatusBar();

void editorDrawMsgBar();

int is_keyword(const char *word);

void highlight_buffer(const char *buffer);

void editorRefreshScreen();

void editorSetStatusMsg(const char *fmt, ...);

void editorRowInsertChar(erow *row, int at, int c);

void editorInsertChar(int isundoredo, int c);

void editorInsertNewline();

void editorRowDelChar(erow *row, int at);

void editorDelChar(int isundoredo);

char *editorPrompt(char *prompt, void (*callback)(char *, int));

int isPrintable(int c);

void editorMoveCursor(int c);

void editorProcessKeypress();

void getWindowSize(int *rows, int *cols);

void editorUpdateSyntax(erow *row);

void initEditor();

void editorUpdateRow(erow *row);

void editorInsertRow(int at, char *s, size_t len);

void editorFreeRow(erow *row);

void editorDelRow(int at);

char *editorRowsToStr(int *buflen);

void editorOpen(char *filename);

void editorSave();

void editorFindCallback(char *query, int key);

void editorFind();

void undo_push(undo_stack *u, redo_stack *r, int ch, int ins, int isredo);

void redo_push(undo_stack *u, redo_stack *r, int ch, int ins, int isundo);

int isemptyu(undo_stack *u);

int isemptyr(redo_stack *r);

int undo_pop(undo_stack *u, redo_stack *r);

int redo_pop(undo_stack *u, redo_stack *r);

void undo(undo_stack *u, redo_stack *r);

void redo(undo_stack *u, redo_stack *r);

void undo_push(undo_stack *u, redo_stack *r, int ch, int ins, int isredo) {
	stack *nn = malloc(sizeof(stack));
	nn->ch = ch;
	if(isredo) {
		nn->cx = r->top->cx;
		nn->cy = r->top->cy;
	}
	else {
		nn->cx = E.cx;
		nn->cy = E.cy;
	}
	nn->ins = ins;
	nn->next = u->top;
	u->top = nn;
}

void redo_push(undo_stack *u, redo_stack *r, int ch, int ins, int isundo) {
	stack *nn = malloc(sizeof(stack));
	nn->ch = ch;
	if(isundo) {
		nn->cx = u->top->cx;
		nn->cy = u->top->cy;
	}
	else {
		nn->cx = E.cx;
		nn->cy = E.cy;
	}
	nn->ins = ins;
	nn->next = r->top;
	r->top = nn;
}

int isemptyu(undo_stack *u) {
	return (u->top == NULL);
}

int isemptyr(redo_stack *r) {
	return (r->top == NULL);
}

int undo_pop(undo_stack *u, redo_stack *r) {
	stack *p = u->top;
	int c = p->ch;
	if(p->ins == 1)
		redo_push(u, r, c, 0, 1);
	else
		redo_push(u, r, c, 1, 1);
	u->top = p->next;
	free(p);
	return c;
}

int redo_pop(undo_stack *u, redo_stack *r) {
	stack *p = r->top;
	int c = p->ch;
	if(p->ins == 1)
		undo_push(u, r, c, 0, 1);
	else
		undo_push(u, r, c, 1, 1);
	r->top = p->next;
	free(p);
	return c;
}

void undo(undo_stack *u, redo_stack *r) {
	if(!isemptyu(u)) {
		E.cx = u->top->cx;
		E.cy = u->top->cy;
		if(u->top->ins == 1) {
			if(u->top->ch == '\n') {
				editorInsertNewline(0);
				undo_pop(u, r);
				return;
			}
			editorInsertChar(0, undo_pop(u, r));
		}
		else {
			editorDelChar(0);
			undo_pop(u, r);
		}
	}
}

void redo(undo_stack *u, redo_stack *r) {
	if(!isemptyr(r)) {
		E.cx = r->top->cx;
		E.cy = r->top->cy;
		if(r->top->ins == 1) {
			if(r->top->ch == '\n') {
				editorInsertNewline(0);
				redo_pop(u, r);
				return;
			}
			editorInsertChar(0, redo_pop(u, r));
		}
		else {
			editorDelChar(0);
			redo_pop(u, r);
		}
	}
}

void die(const char *s) {
	perror(s);
	exit(1);
}

void abAppend(abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);

	if(new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(abuf *ab) {
	free(ab->b);
}

int editorRowCxToRx(erow *row, int cx) {
	int rx = 0;
	for(int j = 0; j < cx; j++) {
		if(row->chars[j] == '\t')
			rx += (TAB_STOP - 1) - (rx % TAB_STOP);
		rx++;
	}
	return rx;
}

int editorRowRxToCx(erow *row, int rx) {
	int cur_rx = 0;
	int cx;
	for(cx = 0; cx < row->size; cx++) {
		if(row->chars[cx] == '\t')
			cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
		cur_rx++;
		if(cur_rx > rx) return cx;
	}
	return cx;
}

void editorScroll() {
	E.rx = 0;
	if(E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}
	if(E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}
	if(E.cy >= E.rowoff + E.rows) {
		E.rowoff = E.cy - E.rows + 1;
	}
	if(E.rx < E.coloff) {
		E.coloff = E.rx;
	}
	if(E.rx >= E.coloff + E.cols) {
		E.coloff = E.rx - E.cols + 1;
	}
}

void editorCharToChtype(abuf *ab) {
	int len = ab->len;
	len += 1;
}

void editorDrawRows(abuf *ab) {
	for(int i = 0; i < E.rows; i++) {
		int filerow = i + E.rowoff;
		if(filerow >= E.numrows) {
			if(!E.dirty) {
				if(E.numrows == 0 && i == E.rows / 3) {
					char welcome[80];
					int welcomelen = snprintf(welcome, sizeof(welcome), "Text editor");
					if(welcomelen > E.cols) welcomelen = E.cols;
					int padding = (E.cols - welcomelen) / 2;
					if(padding) {
						abAppend(ab, "~", 1);
						padding--;
					}
					while(padding--) abAppend(ab, " ", 1);
					abAppend(ab, welcome, welcomelen);
				}
				else {
					abAppend(ab, "~", 1);
				}
			}
			else {
				abAppend(ab, " ", 1);
			}
		}
		else {
			E.line_width = (int)log10(E.numrows) + 1;
			int line_number = filerow + 1;
			char line_number_str[E.line_width + 1];
			snprintf(line_number_str, sizeof(line_number_str), "%*d ", E.line_width, line_number);
			abAppend(ab, line_number_str, strlen(line_number_str));
			abAppend(ab, " ", 1);
			int len = E.row[filerow].rsize - E.coloff;
			if(len < 0) len = 0;
			if(len >= E.cols) len = E.cols - E.line_width - 2;
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}
		abAppend(ab, "\n", 1);
	}
	abAppend(ab, "", 1);
}

void editorDrawStatusBar() {
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%s - %d lines %s", E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
	if(len > E.cols) len = E.cols;
	attron(COLOR_PAIR(1));
	clrtoeol();
	for(int i = 0; i < len; i++) {
		addch(status[i]);
	}
	while(len < E.cols) {
		if(E.cols - len - 1 == rlen) {
			for(int i = 0; i < rlen; i++)
				addch(rstatus[i]);
			break;
		}
		else
			addch(' ');
		len++;
	}
	addch('\n');
	attroff(COLOR_PAIR(1));
}

void editorDrawMsgBar() {
	if(strlen(E.statusmsg) && time(NULL) - E.statusmsg_time < 3) {
		clrtoeol();
		addstr(E.statusmsg);
	}
	else
		clrtoeol();
}

#define MAX_LINE_LENGTH 1024

const char *keywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double",
    "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register",
    "restrict", "return", "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
    "union", "unsigned", "void", "volatile", "while"
};

#define NUM_KEYWORDS (sizeof(keywords) / sizeof(keywords[0]))

int is_keyword(const char *word) {
    for(long unsigned int i = 0; i < NUM_KEYWORDS; i++) {
        if(strcmp(word, keywords[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

void highlight_buffer(const char *buffer) {
	char word[MAX_LINE_LENGTH];
	int word_len = 0;
	int in_string = 0;
	int in_comment = 0;

	for(int i = 0; buffer[i] != '\0'; i++) {
		char ch = buffer[i];
		if(in_comment) {
			attron(COLOR_PAIR(4));
			printw("%c", ch);
			if(ch == '\n') {
				in_comment = 0;
			}
			attroff(4);
		}
		else if(in_string) {
			attron(COLOR_PAIR(3));
			printw("%c", ch);
			if(ch == '"') {
				in_string = 0;
			}
			attroff(3);
		}
		else if(ch == '"') {
			in_string = 1;
			attron(COLOR_PAIR(3));
			printw("%c", ch);
			attroff(3);
		}
		else if(ch == '/' && buffer[i + 1] == '/') {
			in_comment = 1;
			attron(COLOR_PAIR(4));
			printw("%c%c", ch, buffer[i + 1]);
			i++;
			attroff(4);
		}
		else if(isspace(ch) || ispunct(ch)) {
			attron(COLOR_PAIR(5));
			printw("%c", ch);
			attroff(5);
		}
		else if(isdigit(ch)) {
			attron(COLOR_PAIR(6));
			printw("%c", ch);
			attroff(6);
		}
		else {
			word[word_len++] = ch;
			if(!isalnum(buffer[i + 1])) {
				word[word_len] = '\0';
				if(is_keyword(word)) {
					attron(COLOR_PAIR(2));
					printw("%s", word);
					attroff(2);
				}
				else {
					attron(COLOR_PAIR(5));
					printw("%s", word);
					attroff(5);
				}
				word_len = 0;
			}
		}
	}
}

void editorRefreshScreen() {
	editorScroll();
	getWindowSize(&E.cols, &E.rows);

	abuf ab = ABUF_INIT;

	curs_set(0);
	move(0, 0);
	editorDrawRows(&ab);
	highlight_buffer(ab.b);
	editorDrawStatusBar();
	editorDrawMsgBar();
	refresh();
	move(E.cy - E.rowoff, E.rx - E.coloff + E.line_width + 1);
	curs_set(2);
	abFree(&ab);
}

void editorSetStatusMsg(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

void editorRowInsertChar(erow *row, int at, int c) {
	if(at < 0 || at > row->size) at = row->size;
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty = 1;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty = 1;
}

void editorInsertChar(int isundoredo, int c) {
	if(E.cy == E.numrows)
		editorInsertRow(E.numrows, "", 0);
	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	E.cx++;
	if(isundoredo)
		undo_push(&E.u, &E.r, c, 0, 0);
}

void editorInsertNewline(int isundoredo) {
	if (E.cx == 0) {
		editorInsertRow(E.cy, "", 0);
	}
	else {
		erow *row = &E.row[E.cy];
		editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
		row = &E.row[E.cy];
		row->size = E.cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	if(isundoredo)
		undo_push(&E.u, &E.r, '\n', 0, 0);
	E.cy++;
	E.cx = 0;
}

void editorRowDelChar(erow *row, int at) {
	if(at < 0 || at >= row->size) return;
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editorUpdateRow(row);
	E.dirty = 1;
}

void editorDelChar(int isundoredo) {
	if(E.cx == 0 && E.cy == 0) return;
	if(E.cy == E.numrows) {
		if(E.cy == 0) return;
		E.cx = E.row[E.cy - 1].size;
		E.cy--;
		return;
	}

	erow *row = &E.row[E.cy];
	if(E.cx > 0) {
		if(isundoredo)
			undo_push(&E.u, &E.r, E.row[E.cy].chars[E.cx - 1], 1, 0);
		editorRowDelChar(row, E.cx - 1);
		E.cx--;
	}
	else {
	    if(isundoredo)
    		undo_push(&E.u, &E.r, '\n', 1, 0);
	    E.cx = E.row[E.cy - 1].size;
	    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
	    editorDelRow(E.cy);
	    E.cy--;
	}
}

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
	size_t bufsize = 128;
	char *buf = malloc(bufsize);
	size_t buflen = 0;
	buf[0] = '\0';
	while(1) {
		editorSetStatusMsg(prompt, buf);
		editorRefreshScreen();
		int c = getch();
		if(c == KEY_DC || c == ctrl('h') || c == KEY_BACKSPACE) {
			if(buflen != 0) buf[--buflen] = '\0';
		}
		else if(c == 27) {
			editorSetStatusMsg("");
			if(callback) callback(buf, c);
			free(buf);
			return NULL;
		}
		else if(c == 10) {
			if (buflen != 0) {
			  editorSetStatusMsg("");
			  if(callback) callback(buf, c);
			  return buf;
			}
		}
		else if(!iscntrl(c) && c < 128) {
			if(buflen == bufsize - 1) {
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}
		if(callback) callback(buf, c);
	}
}

int isPrintable(int c) {
	return (c == '\t' || (c >= 32 && c <= 126));
}

void editorMoveCursor(int c) {
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	switch(c) {
		case KEY_LEFT:
			if(E.cx != 0) E.cx--;
			else if(E.cy > 0) {
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case KEY_RIGHT:
			if(row && E.cx < row->size) E.cx++;
			else if(row && E.cx == row->size) {
				E.cy++;
				E.cx = 0;
			}
			break;
		case KEY_UP:
			if(E.cy != 0) E.cy--;
			break;
		case KEY_DOWN:
			if(E.cy < E.numrows) E.cy++;
			break;
		case KEY_HOME:
			E.cx = 0;
			break;
		case KEY_END:
			if(E.cy < E.numrows) E.cx = E.row[E.cy].size;
			break;
		case 338:
		case 339:
			int times = E.rows;
			if(c == 339) {
				E.cy = E.rowoff;
				while(times--)
					if(E.cy != 0) E.cy--;
				break;
			}
			else if(c == 338) {
				E.cy = E.rowoff + E.rows - 1;
				if(E.cy > E.numrows) E.cy = E.numrows;
				while(times--)
					if(E.cy < E.numrows) E.cy++;
				break;
			}
			break;
	}
}

void editorProcessKeypress() {
	MEVENT event;
	static int quit_times = QUIT_TIMES;
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int c = getch();

	switch(c) {
		case ctrl('q'):
			if(E.dirty && quit_times > 0) {
				editorSetStatusMsg("Unsaved file. Press Ctrl-Q once more to quit");
				quit_times--;
				return;
			}
			endwin();
			exit(0);
			break;
		case ctrl('s'):
			editorSave();
			break;
		case ctrl('z'):
			undo(&E.u, &E.r);
			break;
		case ctrl('y'):
			redo(&E.u, &E.r);
			break;
		case KEY_HOME:
			editorMoveCursor(c);
			break;
		case KEY_END:
			editorMoveCursor(c);
			break;
		case 338:
		case 339:
			editorMoveCursor(c);
				break;
		case ctrl('f'):
			editorFind();
			break;
		case KEY_ENTER:
		case ctrl('j'):
			editorInsertNewline(1);
			break;
		case KEY_BACKSPACE:
		case ctrl('h'):
		case KEY_DC:
			if(c == KEY_DC) {
				if(row && E.cx < row->size) E.cx++;
				else if(row && E.cx == row->size) {
					E.cy++;
					E.cx = 0;
				}
			}
			editorDelChar(1);
			break;
		case KEY_LEFT:
			editorMoveCursor(c);
			break;
		case KEY_RIGHT:
			editorMoveCursor(c);
			break;
		case KEY_UP:
			editorMoveCursor(c);
			break;
		case KEY_DOWN:
			editorMoveCursor(c);
			break;
		case 27:
			int ch = getch();
			if(ch == -1) break;
			else {
				switch(ch) {
					case 'j': editorMoveCursor(KEY_UP); break;
					case 'h': editorMoveCursor(KEY_LEFT); break;
					case 'k': editorMoveCursor(KEY_DOWN); break;
					case 'l': editorMoveCursor(KEY_RIGHT); break;
					case 'n': editorMoveCursor(KEY_HOME); break;
					case 'm': editorMoveCursor(KEY_END); break;
				}
			}
			break;
		case KEY_MOUSE:
			if(c == KEY_MOUSE) {
				if(getmouse(&event) == OK) {
					if(event.bstate & BUTTON4_PRESSED)
						editorMoveCursor(KEY_UP);
					else if(event.bstate & BUTTON5_PRESSED)
						editorMoveCursor(KEY_DOWN);
					else {
						E.cy = event.y + E.rowoff;
						E.rx = event.x + E.line_width;
						E.cx = editorRowRxToCx(&E.row[E.cy], event.x - E.line_width - 1);
					}
				}
			}
		default:
			if(isPrintable(c))
				editorInsertChar(1, c);
			break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if(E.cx > rowlen)
		E.cx = rowlen;

	quit_times = QUIT_TIMES;
}

void getWindowSize(int *rows, int *cols) {
	getmaxyx(stdscr, *cols, *rows);
	E.rows -= 2;
}

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.numrows = 0;
	E.line_width = 0;
	E.coloff = 0;
	E.row = NULL;
	E.dirty = 0;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.u.top = E.r.top = NULL;
	initscr();
	start_color();
	clear();
	raw();
	keypad(stdscr, TRUE);
	noecho();
	scrollok(stdscr, FALSE);
	mousemask(ALL_MOUSE_EVENTS, NULL);

	init_color(COLOR_CYAN, 188, 188, 211);
	init_color(COLOR_RED, 1000, 0, 0);
	init_color(COLOR_BLUE, 188, 737, 929);

	init_pair(1, COLOR_BLACK, COLOR_WHITE);
	init_pair(2, COLOR_BLUE, COLOR_CYAN);
	init_pair(3, COLOR_YELLOW, COLOR_CYAN);
	init_pair(4, COLOR_GREEN, COLOR_CYAN);
	init_pair(5, COLOR_WHITE, COLOR_CYAN);
	init_pair(6, COLOR_RED, COLOR_CYAN);
	init_pair(10, COLOR_WHITE, COLOR_CYAN);

	bkgd(COLOR_PAIR(10));
	getWindowSize(&E.cols, &E.rows);
}

void editorUpdateRow(erow *row) {
	int tabs = 0;
	for(int j = 0; j < row->size; j++)
		if(row->chars[j] == '\t') tabs++;

	free(row->render);
	row->render = malloc(row->size + tabs * (TAB_STOP - 1) + 1);

	int idx = 0;
	for(int j = 0; j < row->size; j++) {
		if(row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while(idx % TAB_STOP != 0) row->render[idx++] = ' ';
		}
		else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len) {
	if(at < 0 || at > E.numrows) return;
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
	memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	editorUpdateRow(&E.row[at]);

	E.numrows++;
	E.dirty = 1;
}

void editorFreeRow(erow *row) {
	free(row->render);
	free(row->chars);
}

void editorDelRow(int at) {
	if (at < 0 || at >= E.numrows) return;
	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
	E.numrows--;
	E.dirty = 1;
}

char *editorRowsToStr(int *buflen) {
	int totlen = 0;
	for(int i = 0; i < E.numrows; i++)
		totlen += E.row[i].size + 1;
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;
	for(int i = 0; i < E.numrows; i++) {
		memcpy(p, E.row[i].chars, E.row[i].size);
		p += E.row[i].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorOpen(char *filename) {
	initEditor();
	free(E.filename);
	E.filename = strdup(filename);
	FILE *fp = fopen(filename, "r");
	if(!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while((linelen = getline(&line, &linecap, fp)) != -1) {
		while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		editorInsertRow(E.numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave() {
	if(E.filename == NULL) {
		E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
		if(E.filename == NULL) {
			editorSetStatusMsg("Save aborted");
			return;
		}
	}

	int len;
	char *buf = editorRowsToStr(&len);
	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if(fd != -1) {
		if(ftruncate(fd, len) != -1) {
			if(write(fd, buf, len) != -1) {
				close(fd);
				free(buf);
				E.dirty = 0;
				editorSetStatusMsg("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	editorSetStatusMsg("Can't save! I/O error : %s", strerror(errno));
}

void editorFindCallback(char *query, int key) {
	static int last_match = -1;
	static int direction = 1;

	if(key == 10 || key == 27) {
		last_match = -1;
		direction = 1;
		return;
	} 
	else if(key == KEY_RIGHT || key == KEY_DOWN) {
		direction = 1;
	}
	else if(key == KEY_LEFT || key == KEY_UP) {
		direction = -1;
	}
	else {
		last_match = -1;
		direction = 1;
	}

	if(last_match == -1) direction = 1;
	int current = last_match;

	for(int i = 0; i < E.numrows; i++) {
		current += direction;
		if(current == -1) current = E.numrows - 1;
		else if(current == E.numrows) current = 0;

		erow *row = &E.row[current];
		char *match = strstr(row->render, query);
		if(match) {
			last_match = current;
			E.cy = current;
			E.cx = editorRowRxToCx(row, match - row->render);
			E.rowoff = E.numrows;
			break;
		}
	}
}

void editorFind() {
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;

	char *query = editorPrompt("Search: %s (Use ESC/Arrow/Enter)", editorFindCallback);
	if(query)
		free(query);
	else {
		E.cx = saved_cx;
		E.cy = saved_cy;
		E.coloff = saved_coloff;
		E.rowoff = saved_rowoff;
	}
}

int main(int argc, char *argv[]) {

	if(argc >= 2) {
		editorOpen(argv[1]);
	}
	else {
		initEditor();
	}

	editorSetStatusMsg("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-Z = undo | Ctrl-Y = redo");

	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}
