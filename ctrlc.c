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
#define LINENUM_MARGIN 4

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/* data */
typedef struct erow {
	int idx; //index for each erow within the file
	int size;
	int render_size;
	char* chars;
	char* render;
	unsigned char* hl; //stands for highlight
	int hl_open_comment;
} erow;

struct editorConfig {
	int cursor_x, cursor_y;
	int render_x;
	int rowoffset;
	int coloffset;
	int screenrows;
	int screencols;
	struct termios orig_termios;
	struct editorSyntax* syntax;
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

enum editorHighlight {
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH
};

struct editorSyntax {
	char* filetype;
	char** filematch;
	char** keywords;
	char* signleline_comment_start;
	char* multiline_comment_start;
	char* multiline_comment_end;
	int flags;
};

struct editorConfig E;

/* filetypes */
char* C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char* C_HL_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return", "else",
	"struct", "union", "typedef", "static", "enum", "class", "case",

	"int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
	"void|", NULL
};

struct editorSyntax HLDB[] = {
	{
		"C",
		C_HL_extensions,
		C_HL_keywords,
		"//",
		"/*",
		"*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/* terminal functions declarations */
void disableRawMode();
void enableRawMode();
void quit_error(const char*); // program dies with error
int editorReadKey();
int getCursorPosition(int*, int*);
int getWindowSize(int*, int*);

/* syntax highlighting func declarations */
int is_separator(int c) {
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}
void editorUpdateSyntax(erow*);
int editorSyntaxToColor(int);
void editorSelectSyntaxHighlight();

/* row operations func declarations */
void editorInsertRow(int, char*, size_t);
void editorUpdateRow(erow*);
int editorRowCxToRx(erow*, int);
void editorRowInsertChar(erow*, int, int);
void editorRowDelChar(erow*, int);
void editorFreeRow(erow*);
void editorDelRow(int at);
void editorRowAppendString(erow*, char*, size_t);
int editorRowRxToCx(erow*, int);


/* editor operations func declarations */
void editorInsertChar(int);
void editorDelChar();
void editorInsertNewline();

/* file input/ouput func declarations */
void editorOpen(char*);
char* editorRowsToString(int*);
void editorSave();

/* find func declarations */
void editorFind();
void editorFindCallback(char*, int);

/* input func declarations */
void editorMoveCursor(int);
void editorProcessKeypress();
char* editorPrompt(char*, void (*callback)(char*, int));

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

	editorSetStatusMessage(
			"HELP: Ctrl+Q = quit | Ctrl+S = save | Ctrl+F = find");

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
	E.syntax = NULL;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
		quit_error("getWindowSize error in initEditor");
	}
	E.screencols -= LINENUM_MARGIN;

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

/* syntax highlighting func realization */
void editorUpdateSyntax(erow* row) {
	row->hl = realloc(row->hl, row->render_size);
	memset(row->hl, HL_NORMAL, row->render_size);

	if (E.syntax == NULL) return;

	char** keywords = E.syntax->keywords;

	char* scs = E.syntax->signleline_comment_start; //scs stands for singleline comment start
	char* mcs = E.syntax->multiline_comment_start;
	char* mce = E.syntax->multiline_comment_end;

	int scs_len = scs ? strlen(scs) : 0;
	int mcs_len = mcs ? strlen(mcs) : 0;
	int mce_len = mce ? strlen(mce) : 0;

	int prev_sep = 1;
	int in_string = 0;
	int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

	int i = 0;
	while (i < row->render_size) {
		char c = row->render[i];
		unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

		if (scs_len && !in_string && !in_comment) {
			if (!strncmp(&row->render[i], scs, scs_len)) {
				memset(&row->hl[i], HL_COMMENT, row->render_size - i);
				break;
			}
		}

		if (mcs_len && mce_len && !in_string) {
			if (in_comment) {
				row->hl[i] = HL_MLCOMMENT;
				if (!strncmp(&row->render[i], mce, mce_len)) {
					memset(&row->hl[i], HL_MLCOMMENT, mce_len);
					i += mce_len;
					in_comment = 0;
					prev_sep = 1;
					continue;
				}
				else {
					++i;
					continue;
				}
			}
			else if (!strncmp(&row->render[i], mcs, mcs_len)) {
				memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
				i += mcs_len;
				in_comment = 1;
				continue;
			}
		}

		if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
			if (in_string) {
				row->hl[i] = HL_STRING;

				if (c == '\\' && i + 1 < row->render_size) {
					row->hl[i + 1] = HL_STRING;
					i += 2;
					continue;
				}

				if (c == in_string) in_string = 0;
				++i;
				prev_sep = 1;
				continue;
			}
			else {
				if (c == '"' || c == '\'') {
					in_string = c;
					row->hl[i] = HL_STRING;
					++i;
					continue;
				}
			}
		}

		if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
			if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
					(c == '.' && prev_hl == HL_NUMBER)) {
				row->hl[i] = HL_NUMBER;
				++i;
				prev_sep = 0;
				continue;
			}
		}

		if (prev_sep) {
			int j;
			for (j = 0; keywords[j]; ++j) {
				int klen = strlen(keywords[j]);
				int kw2 = keywords[j][klen - 1] == '|';

				if (kw2) --klen;

				if (!strncmp(&row->render[i], keywords[j], klen) &&
					is_separator(row->render[i + klen])) {
					memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
					i += klen;
					break;
				}
			}

			if (keywords[j] != NULL) {
				prev_sep = 0;
				continue;
			}
		}

		prev_sep = is_separator(c);
		++i;
	}

	int changed = (row->hl_open_comment != in_comment);
	row->hl_open_comment = in_comment;
	if (changed && row->idx + 1 < E.numrows) {
		editorUpdateSyntax(&E.row[row->idx + 1]);
	}
}

int editorSyntaxToColor(int hl) {
	switch (hl) {
		case HL_COMMENT:
		case HL_MLCOMMENT: return 36;
		case HL_KEYWORD1: return 33;
		case HL_KEYWORD2: return 32;
		case HL_STRING: return 35;
		case HL_NUMBER: return 31;
		case HL_MATCH: return 34;

		default: return 37;
	}
}

void editorSelectSyntaxHighlight() {
	E.syntax = NULL;
	if (E.filename == NULL) return;

	char* ext = strrchr(E.filename, '.');

	for (unsigned int j = 0; j < HLDB_ENTRIES; ++j) {
		struct editorSyntax* s = &HLDB[j];
		unsigned int i = 0;

		while (s->filematch[i]) {
			int is_ext = (s->filematch[i][0] == '.');

			if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
				(!is_ext && strstr(E.filename, s->filematch[i]))) {
				E.syntax = s;

				for (int filerow = 0; filerow < E.numrows; ++filerow) {
					editorUpdateSyntax(&E.row[filerow]);
				}

				return;
			}
			++i;
		}
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

int editorRowRxToCx(erow* row, int rx) {
	int cur_rx = 0; //rx stands for render x
	int cx; //cx stands for cursor_x
	for (cx = 0; cx < row->size; ++cx) {
		if (row->chars[cx] == '\t') {
			cur_rx += (CTRLC_TAB_STOP - 1) - (cur_rx % CTRLC_TAB_STOP);
		}
		++cur_rx;

		if (cur_rx > rx) return cx;
	}

	return cx;
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

	editorUpdateSyntax(row);
}

void editorInsertRow(int at, char* string, size_t len) {
	if (at < 0 || at > E.numrows) return;

	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
	memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

	for (int j = at + 1; j <= E.numrows; ++j) {
		++E.row[j].idx;
	}

	E.row[at].idx = at;

	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, string, len);
	E.row[at].chars[len] = '\0';

	E.row[at].render_size = 0;
	E.row[at].render = NULL;
	E.row[at].hl = NULL;
	E.row[at].hl_open_comment = 0;
	editorUpdateRow(&E.row[at]);

	++E.numrows;
	++E.dirty;
}

void editorFreeRow(erow* row) {
	free(row->render);
	free(row->chars);
	free(row->hl);
}

void editorDelRow(int at) {
	if (at < 0 || at >= E.numrows) return;

	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));

	for (int j = at; j < E.numrows - 1; ++j) {
		--E.row[j].idx;
	}

	--E.numrows;
	++E.dirty;
}

void editorRowAppendString(erow* row, char* s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);

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
		editorInsertRow(E.numrows, "", 0);
	}

	editorRowInsertChar(&E.row[E.cursor_y], E.cursor_x, c);
	E.cursor_x++;
}

void editorInsertNewline() {
	if (E.cursor_x == 0) {
		editorInsertRow(E.cursor_y, "", 0);
	}
	else {
		erow* row = &E.row[E.cursor_y];
		editorInsertRow(E.cursor_y + 1, &row->chars[E.cursor_x], row->size - E.cursor_x);
		row = &E.row[E.cursor_y];
		row->size = E.cursor_x;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}

	++E.cursor_y;
	E.cursor_x = 0;
}

void editorDelChar() {
	if (E.cursor_y == E.numrows) return;
	if (E.cursor_x == 0 && E.cursor_y == 0) return;

	erow* row = &E.row[E.cursor_y];
	if (E.cursor_x > 0) {
		editorRowDelChar(row, E.cursor_x - 1);
		--E.cursor_x;
	}
	else {
		E.cursor_x = E.row[E.cursor_y - 1].size;
		editorRowAppendString(&E.row[E.cursor_y - 1], row->chars, row->size);
		editorDelRow(E.cursor_y);
		--E.cursor_y;
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

	editorSelectSyntaxHighlight();

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
		editorInsertRow(E.numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave() {
	if (E.filename == NULL) {
		E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
		if (E.filename == NULL) {
			editorSetStatusMessage("Save aborted!");
			return;
		}
		editorSelectSyntaxHighlight();
	}

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

/* find func realization */
void editorFindCallback(char* query, int key) {
	static int last_match = -1;
	static int direction = 1;

	static int saved_hl_line;
	static char* saved_hl = NULL;

	if (saved_hl) {
		memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].render_size);
		free(saved_hl);
		saved_hl = NULL;
	}

	if (key == '\r' || key == '\x1b') {
		last_match = -1;
		direction = 1;
		return;
	}
	else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
	}
	else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
	}
	else {
		last_match = -1;
		direction = 1;
	}

	if (last_match == -1) {
		direction = 1;
	}
	int current = last_match;
	for (int i = 0; i < E.numrows; ++i) {
		current += direction;
		if (current == -1) {
			current = E.numrows - 1;
		}
		else if (current == E.numrows) {
			current = 0;
		}

		erow* row = &E.row[current];
		char* match = strstr(row->render, query);
		if (match) {
			last_match = current;
			E.cursor_y = current;
			E.cursor_x = editorRowRxToCx(row, match - row->render);
			E.rowoffset = E.numrows;

			saved_hl_line = current;
			saved_hl = malloc(row->render_size);
			memcpy(saved_hl, row->hl, row->render_size);
			memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
			break;
		}
	}
}

void editorFind() {
	int saved_cursor_x = E.cursor_x;
	int saved_cursor_y = E.cursor_y;
	int saved_coloffset = E.coloffset;
	int saved_rowoffset = E.rowoffset;

	char* query = editorPrompt("Search: %s (press ESC/Arrows/Enter)", editorFindCallback);

	if (query) {
		free(query);
	}
	else {
		E.cursor_x = saved_cursor_x;
		E.cursor_y = saved_cursor_y;
		E.coloffset = saved_coloffset;
		E.rowoffset = saved_rowoffset;
	}
}

/* input func realization */
char* editorPrompt(char* prompt, void (*callback)(char*, int)) {
	size_t buffsize = 128;
	char* buff = malloc(buffsize);

	size_t bufflen = 0;
	buff[0] = '\0';

	while (1) {
		editorSetStatusMessage(prompt, buff);
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == '\x1b') {
			editorSetStatusMessage("");
			if (callback) {
				callback(buff, c);
			}
			free(buff);
			return NULL;
		}
		else if (c == DELETE || c == CTRL_KEY('h') || c == BACKSPACE) {
				if (bufflen != 0) {
					buff[--bufflen] = '\0';
				}
		}
		else if (c == '\r') {
			if (bufflen != 0) {
				editorSetStatusMessage("");
				if (callback) {
					callback(buff, c);
				}
				return buff;
			}
		}
		else if (!iscntrl(c) && c < 128) {
			if (bufflen == buffsize - 1) {
				buffsize *= 2;
				buff = realloc(buff, buffsize);
			}
			buff[bufflen++] = c;
			buff[bufflen] = '\0';
		}

		if (callback) {
			callback(buff, c);
		}
	}
}

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
			if (E.cursor_y < E.numrows - 1) {
				++E.cursor_y;
			}
			break;
	}

	if (E.numrows == 0) {
		E.cursor_y = 0;
	}
	else if (E.cursor_y > E.numrows - 1) {
		E.cursor_y = E.numrows - 1;
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
			editorInsertNewline();
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

		case CTRL_KEY('f'):
			editorFind();
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
			char linenum_buf[16];
			if (filerow == E.cursor_y) {
				snprintf(linenum_buf, sizeof(linenum_buf), ">%2d ", filerow + 1);
			}
			else {
				snprintf(linenum_buf, sizeof(linenum_buf), " %2d ", filerow + 1);
			}
			abAppend(ab, linenum_buf, strlen(linenum_buf));

			int len = E.row[filerow].render_size - E.coloffset;
			if (len < 0) {
				len = 0;
			}
			if (len > E.screencols) {
				len = E.screencols;
			}
			char* c = &E.row[filerow].render[E.coloffset];
			unsigned char* hl = &E.row[filerow].hl[E.coloffset];
			int current_color = -1;
			for (int j = 0; j < len; ++j) {
				if (iscntrl(c[j])) {
					char sym = (c[j] <= 26) ? '@' + c[j] : '?';
					abAppend(ab, "\x1b[7m", 4);
					abAppend(ab, &sym, 1);
					abAppend(ab, "\x1b[m", 3);
					if (current_color != -1) {
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
						abAppend(ab, buf, clen);
					}
				}
				else if (hl[j] == HL_NORMAL) {
					if (current_color != -1) {
						abAppend(ab, "\x1b[39m", 5);
						current_color = -1;
					}
					abAppend(ab, &c[j], 1);
				}
				else {
					int color = editorSyntaxToColor(hl[j]);
					if (color != current_color) {
						current_color = color;
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
						abAppend(ab, buf, clen);
					}
					abAppend(ab, &c[j], 1);
				}
			}
			abAppend(ab, "\x1b[39m", 5);
			//abAppend(ab, &E.row[filerow].render[E.coloffset], len);
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

	int total_lines = E.numrows > 0 ? E.numrows : 1;
	int current_line = E.numrows > 0 ? E.cursor_y + 1 : 0;
	int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", //rlen stands for render length
			E.syntax ? E.syntax->filetype : "no filetype", current_line, total_lines);
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
			(E.cursor_y - E.rowoffset) + 1, (E.render_x - E.coloffset) + 1 + LINENUM_MARGIN);
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
