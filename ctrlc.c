/* includes */
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

/* defines */
#define CTRL_KEY(k) ((k) & 0x1f) // getting the control key version of the k like ctrl + letter
#define CTRLC_VERSION "1.0"

/* data */
struct editorConfig {
	int cursor_x, cursor_y;
	int screenrows;
	int screencols;
	struct termios orig_termios;
};

struct editorConfig E;

/* terminal functions declarations */
void disableRawMode();
void enableRawMode();
void quit_error(const char*); // program dies with error
char editorReadKey();
int getCursorPosition(int*, int*);
int getWindowSize(int*, int*);

/* input func declarations */
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
void editorRefreshScreen();
void editorDrawRows(struct abuf*);

int main() {
	enableRawMode();
	initEditor();

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

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
		quit_error("getWindowSize error in initEditor");
	}
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

char editorReadKey() {
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

	return c;
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

/* input func realization */
void editorProcessKeypress() {
	char c = editorReadKey();

	switch (c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(EXIT_SUCCESS);
			break;
	}
}

/* output func realization */
void editorDrawRows(struct abuf* ab) {
	for (int i = 0; i < E.screenrows; ++i) {
		if (i == E.screenrows / 3) {
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
		
		abAppend(ab, "\x1b[K", 3); //erase the part of the line to the right of the cursor
		if (i < E.screenrows - 1) {
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6); //hide the cursor
	//abAppend(&ab, "\x1b[2J", 4);
	abAppend(&ab, "\x1b[H", 3);
	editorDrawRows(&ab);

	abAppend(&ab, "\x1b[H", 3);
	abAppend(&ab, "\x1b[?25h", 6); //show the cursor

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}
