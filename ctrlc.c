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
void editorReadKey();
void editorProcessKeypress();


int main() {
	enableRawMode();

	char c;
	while (1) {
		c = '\0';
		if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {
			quit_error("read error in main");
		}
		if (iscntrl(c)) {
			printf("%d\r\n", c);
		}
		else {
			printf("%d ('%c')\r\n", c, c);
		}

		if (c == CTRL_KEY('q')) break;
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

void editorReadKey() {

}

void editorProcessKeypress() {

}
