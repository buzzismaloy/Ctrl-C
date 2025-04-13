/* macros for fine compiling the getline func on every machine */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

/* includes */
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

/* defines */
#define CTRL_KEY(k) ((k) & 0x1f) // getting the control key version of the k like ctrl + letter
#define CTRLC_VERSION "1.0"
#define CTRLC_TAB_STOP 8
#define CTRLC_QUIT_TIMES 2

/* data */
typedef struct erow {
	int size;
	int render_size;
	char* chars;
	char* render;
} erow;

struct editorConfig {
	int cursor_x, cursor_y;
	int render_x;
	int rowoffset;
	int coloffset;
	int screenrows;
	int screencols;
	struct termios orig_termios;
	int numrows;
	erow* row;
	char* filename;
	char statusmsg[80];
	time_t statusmsg_time;
	int dirty; //flag that tells us whether the file was modified or not
};

enum editorKey {
	BACKSPACE = 127,
	ARROW_LEFT = 5000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	PAGE_UP,
	PAGE_DOWN,
	HOME,
	END,
	DELETE
};

struct editorConfig E;

/* terminal functions declarations */
void disableRawMode();
void enableRawMode();
void quit_error(const char*); // program dies with error
int editorReadKey();
int getCursorPosition(int*, int*);
int getWindowSize(int*, int*);

/* row operations func declarations */
void editorAppendRow(char*, size_t);
void editorUpdateRow(erow*);
int editorRowCxToRx(erow*, int);
void editorRowInsertChar(erow*, int, int);
void editorRowDelChar(erow*, int);

/* editor operations func declarations */
void editorInsertChar(int);
void editorDelChar();

/* file input/ouput func declarations */
void editorOpen(char*);
char* editorRowsToString(int*);
void editorSave();

/* input func declarations */
void editorMoveCursor(int);
void editorProcessKeypress();

/* init func declarations */
void initEditor();

/* append buffer, lets make dynamic string type */
struct abuf {
	char* b;
	int len;
};

#define ABUF_INIT {NULL, 0}

/* append buffer functions declaration */
void abAppend(struct abuf*, const char*, int len);
void abFree(struct abuf*);

/* output func declaration */
void editorScroll();
void editorRefreshScreen();
void editorDrawRows(struct abuf*);
void editorDrawStatusBar(struct abuf*);
void editorSetStatusMessage(const char*, ...);
void editorDrawMessageBar(struct abuf* ab);

int main(int argc, char* argv[]) {
	enableRawMode();
	initEditor();
	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	editorSetStatusMessage("HELP: Ctrl+Q = quit | Ctrl+S = save");

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return EXIT_SUCCESS;
}

/* append buffer functions realization */
void abAppend(struct abuf* ab, const char* s, int len) {
	char* new = realloc(ab->b, ab->len + len);
	if (new == NULL) {
		fprintf(stderr, "\nMemory allocation error in abAppend!\n");
		return;
	}

	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf* ab) {
	free(ab->b);
}

/* init functions realization */
void initEditor() {
	E.cursor_x = 0;
	E.cursor_y = 0;
	E.render_x = 0;
	E.numrows = 0;
	E.row = NULL;
	E.filename = NULL;
	E.rowoffset = 0;
	E.coloffset = 0;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.dirty = 0;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
		quit_error("getWindowSize error in initEditor");
	}

	E.screenrows -= 2;
}

/* terminal functions realization */
void quit_error(const char* s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(EXIT_FAILURE);
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
		quit_error("disableRawMode error");
	}
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
		quit_error("enableRawMode; tcgetattr error");
	}
	if (atexit(disableRawMode)) {
		fprintf(stderr, "\n\nCant registrate disableRawMode\n");
		return;
	}

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
		quit_error("enableRawMode; tcsetattr error");
	}
}

int editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
	//here read() waits untill user press key by reading nbytes = 1 from STDIN_FILENO
	//read() returns number of bytes read from fd(here it is STDIN_FILENO)
	//and if user pressed key, the loop is over and read character is returned
		if (nread == -1 && errno != EAGAIN) {
			quit_error("error in reading key");
		}
	}

	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return HOME;
						case '3': return DELETE;
						case '4': return END;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME;
						case '8': return END;
					}
				}
			}
			else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME;
					case 'F': return END;
				}
			}
		}
		else if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'H': return HOME;
				case 'F': return END;
			}
		}

		return '\x1b';
	}
	else {
		return c;
	}
}

int getCursorPosition(int* rows, int* cols) {
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
		return -1;
	}

	char buf[32];
	unsigned int i = 0;

	while (i < sizeof(buf) -1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) {
			break;
		}

		if (buf[i] == 'R') {
			break;
		}
		++i;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') {
		return -1;
	}
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
		return -1;
	}

	return 0;
}

int getWindowSize(int* rows, int* cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
			return -1;
		}

		return getCursorPosition(rows, cols);
	}
	else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;

		return 0;
	}
}

/* row operations func realization */
int editorRowCxToRx(erow* row, int cx) {
	int rx = 0;
	for (int j = 0; j < cx; ++j) {
		if (row->chars[j] == '\t') {
			rx += (CTRLC_TAB_STOP - 1) - (rx % CTRLC_TAB_STOP);
		}
		++rx;
	}

	return rx;
}

void editorUpdateRow(erow* row) {
	int tabs = 0;
	for (int i = 0; i < row->size; ++i) {
		if (row->chars[i] == '\t') ++tabs;
	}

	free(row->render);
	row->render = malloc(row->size + tabs * (CTRLC_TAB_STOP - 1) + 1);

	int idx = 0;
	for (int j = 0; j < row->size; ++j) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % CTRLC_TAB_STOP != 0) {
				row->render[idx++] = ' ';
			}
		}
		else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->render_size = idx;
}

void editorAppendRow(char* string, size_t len) {
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

	int new_line = E.numrows;
	E.row[new_line].size = len;
	E.row[new_line].chars = malloc(len + 1);
	memcpy(E.row[new_line].chars, string, len);
	E.row[new_line].chars[len] = '\0';

	E.row[new_line].render_size = 0;
	E.row[new_line].render = NULL;
	editorUpdateRow(&E.row[new_line]);

	++E.numrows;
	++E.dirty;
}

void editorRowInsertChar(erow* row, int at, int c) {
	if (at < 0 || at > row->size) {
		at = row->size;
	}
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	++E.dirty;
}

void editorRowDelChar(erow* row, int at) {
	if (at < 0 || at >= row->size) return;

	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editorUpdateRow(row);
	E.dirty++;
}

/* editor operations func realization */
void editorInsertChar(int c) {
	if (E.cursor_y == E.numrows) {
		editorAppendRow("", 0);
	}

	editorRowInsertChar(&E.row[E.cursor_y], E.cursor_x, c);
	E.cursor_x++;
}

void editorDelChar() {
	if (E.cursor_y == E.numrows) return;

	erow* row = &E.row[E.cursor_y];
	if (E.cursor_x > 0) {
		editorRowDelChar(row, E.cursor_x - 1);
		--E.cursor_x;
	}
}

/* file input/output func realization */
char* editorRowsToString(int* bufflen) {
	int totallen = 0;
	for (int j = 0; j < E.numrows; ++j) {
		totallen += E.row[j].size + 1;
	}
	*bufflen = totallen;

	char* buff = malloc(totallen);
	char* p= buff;

	for (int j = 0; j < E.numrows; ++j) {
		memcpy(p, E.row[j].chars, E.row[j].size);
		p += E.row[j].size;
		*p = '\n';
		++p;
	}

	return buff;
}

void editorOpen(char* filename) {
	free(E.filename);
	E.filename = strdup(filename);

	FILE* fp = fopen(filename, "r");
	if (!fp) {
		quit_error("error opening file; editorOpen func");
	}
	char* line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
							line[linelen - 1] == '\r')) {
			--linelen;
		}
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave() {
	if (E.filename == NULL) return;

	int len;
	char* buff = editorRowsToString(&len);

	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buff, len) == len) {
				close(fd);
				free(buff);
				E.dirty = 0;
				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}
	free(buff);
	editorSetStatusMessage("Cant save! I/O error: %s", strerror(errno));
}

/* input func realization */
void editorMoveCursor(int key) {
	erow* row = (E.cursor_y >= E.numrows) ? NULL : &E.row[E.cursor_y];
	switch (key) {
		case ARROW_LEFT:
			if (E.cursor_x != 0) {
				--E.cursor_x;
			}
			else if (E.cursor_y > 0) {
				--E.cursor_y;
				E.cursor_x = E.row[E.cursor_y].size;
			}
			break;
		case ARROW_RIGHT:
			if (row && E.cursor_x < row->size) {
				++E.cursor_x;
			}
			else if (row && E.cursor_x == row->size) {
				++E.cursor_y;
				E.cursor_x = 0;
			}
			break;
		case ARROW_UP:
			if (E.cursor_y != 0) {
				--E.cursor_y;
			}
			break;
		case ARROW_DOWN:
			if (E.cursor_y < E.numrows) {
				++E.cursor_y;
			}
			break;
	}

	row = (E.cursor_y >= E.numrows) ? NULL : &E.row[E.cursor_y];
	int rowlen = row ? row->size : 0;
	if (E.cursor_x > rowlen) {
		E.cursor_x = rowlen;
	}
}

void editorProcessKeypress() {
	static int quit_times = CTRLC_QUIT_TIMES;

	int c = editorReadKey();

	switch (c) {
		case '\r':
			//todo
			break;

		case CTRL_KEY('q'):
			if (E.dirty && quit_times > 0) {
				editorSetStatusMessage(
						"WARNING!!! File has unsaved changes. Press Ctrl+Q %d more times to quit",
						quit_times);
				--quit_times;
				return;
			}
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(EXIT_SUCCESS);
			break;

		case CTRL_KEY('s'):
			editorSave();
			break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
			if (c == PAGE_UP) {
				E.cursor_y = E.rowoffset;
			}
			else if (c == PAGE_DOWN) {
				E.cursor_y = E.rowoffset + E.screenrows - 1;
				if (E.cursor_y > E.numrows) {
					E.cursor_y = E.numrows;
				}
			}

			int scroll_times = E.screenrows;
			while (scroll_times--) {
				editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;
			}

		case HOME:
			E.cursor_x = 0;
			break;

		case END:
			if (E.cursor_y < E.numrows) {
				E.cursor_x = E.row[E.cursor_y].size;
			}
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DELETE:
			if (c == DELETE) {
				editorMoveCursor(ARROW_RIGHT);
			}
			editorDelChar();
			break;

		case CTRL_KEY('l'):
		case '\x1b':
			break;

		default:
			editorInsertChar(c);
			break;
	}

	quit_times = CTRLC_QUIT_TIMES;
}

/* output func realization */
void editorScroll() {
	E.render_x = 0;
	if (E.cursor_y < E.numrows) {
		E.render_x = editorRowCxToRx(&E.row[E.cursor_y], E.cursor_x);
	}

	if (E.cursor_y < E.rowoffset) {
		E.rowoffset = E.cursor_y;
	}

	if (E.cursor_y >= E.rowoffset + E.screenrows) {
		E.rowoffset = E.cursor_y - E.screenrows + 1;
	}

	if (E.render_x < E.coloffset) {
		E.coloffset = E.render_x;
	}

	if (E.render_x >= E.coloffset + E.screencols) {
		E.coloffset = E.render_x - E.screencols + 1;
	}
}

void editorDrawRows(struct abuf* ab) {
	for (int i = 0; i < E.screenrows; ++i) {
		int filerow = i + E.rowoffset;
		if (filerow >= E.numrows) {
			if (E.numrows == 0 && i == E.screenrows / 3) {
				char welcome_msg[80];
				int welcome_msg_len = snprintf(welcome_msg, sizeof(welcome_msg),
						"Ctrl + C editor --> version %s", CTRLC_VERSION);
				if (welcome_msg_len > E.screencols) {
					welcome_msg_len = E.screencols;
				}
				int padding = (E.screencols - welcome_msg_len) / 2;
				if (padding) {
					abAppend(ab, "~>", 2);
					padding -= 2;
				}
				while (padding--) {
					abAppend(ab, " ", 1);
				}
				abAppend(ab, welcome_msg, welcome_msg_len);
			}
			else {
				abAppend(ab, "~>", 2);
			}
		}
		else {
			int len = E.row[filerow].render_size - E.coloffset;
			if (len < 0) {
				len = 0;
			}
			if (len > E.screencols) {
				len = E.screencols;
			}
			abAppend(ab, &E.row[filerow].render[E.coloffset], len);
		}

		abAppend(ab, "\x1b[K", 3); //erase the part of the line to the right of the cursor
		abAppend(ab, "\r\n", 2);

	}
}

void editorDrawStatusBar(struct abuf* ab) {
	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80]; //rstatus stands for render status
	int len = snprintf(status, sizeof(status), "%.20s%s - %d lines",
			E.filename ? E.filename : "[No name]", E.dirty ? "{+}" : "", E.numrows);
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", //rlen stands for render length
			E.cursor_y + 1, E.numrows);
	if (len > E.screencols) {
		len = E.screencols;
	}
	abAppend(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		}
		else {
			abAppend(ab, " ", 1);
			++len;
		}
	}
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf* ab) {
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols) msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5) {
		abAppend(ab, E.statusmsg, msglen);
	}
}

void editorRefreshScreen() {
	editorScroll();

	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6); //hide the cursor
	//abAppend(&ab, "\x1b[2J", 4);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	char buff[32];
	snprintf(buff, sizeof(buff), "\x1b[%d;%dH",
			(E.cursor_y - E.rowoffset) + 1, (E.render_x - E.coloffset) + 1);
	abAppend(&ab, buff, strlen(buff));

	abAppend(&ab, "\x1b[?25h", 6); //show the cursor

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char* format, ...) {
	va_list ap;
	va_start(ap, format);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), format, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}
