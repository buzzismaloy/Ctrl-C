/* includes */
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

/* defines */
#define CTRL_KEY(k) ((k) & 0x1f) // getting the control key version of the k like ctrl + letter

/* data and variables */
struct termios orig_termios;

/* terminal functions declarations */
void disableRawMode();
void enableRawMode();
void quit_error(const char*); // program dies with error
char editorReadKey();

/* input func declarations */
void editorProcessKeypress();

/* output func declaration */
void editorRefreshScreen();

int main() {
	enableRawMode();

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return EXIT_SUCCESS;
}

/* terminal functions realization */
void quit_error(const char* s) {
	perror(s);
	exit(EXIT_FAILURE);
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
		quit_error("disableRawMode error");
	}
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
		quit_error("enableRawMode; tcgetattr error");
	}
	if (atexit(disableRawMode)) {
		fprintf(stderr, "\n\nCant registrate disableRawMode\n");
		return;
	}

	struct termios raw = orig_termios;
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

/* input func realization */
void editorProcessKeypress() {
	char c = editorReadKey();

	switch (c) {
		case CTRL_KEY('q'):
			exit(EXIT_SUCCESS);
			break;
	}
}

/* output func realization */
void editorRefreshScreen() {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
}
