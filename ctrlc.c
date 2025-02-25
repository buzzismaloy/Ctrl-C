#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

struct termios orig_termios;

void disableRawMode();
void enableRawMode();

int main() {
	enableRawMode();

	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
		if (iscntrl(c)) {
			printf("%d\n", c);
		}
		else {
			printf("%d ('%c')\n", c, c);
		}
	}

	return EXIT_SUCCESS;
}

void disableRawMode() {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
	tcgetattr(STDIN_FILENO, &orig_termios);
	if (atexit(disableRawMode)) {
		fprintf(stderr, "\n\nCant registrate disableRawMode\n");
		return;
	}

	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO | ICANON | ISIG);

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
