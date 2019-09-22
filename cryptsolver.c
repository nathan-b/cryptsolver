#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include <ncurses.h>

typedef struct {
	enum {
		NONE,
		MOVE_CURSOR,
		JUMP_CURSOR,
		ADD_LETTER,
		SOLVE_CAESAR,
	} act_type;

	union {
		struct {
			uint32_t row;
			uint32_t col;
		} coords;
		enum {
			DIR_UP,
			DIR_DOWN,
			DIR_LEFT,
			DIR_RIGHT,
		} direction;
		char letter;
	} u;
} action_t;

#define KEY_ESC 27

#define CP_WHITE   1
#define CP_RED     2
#define CP_MAGENTA 3

const uint32_t pad = 10;
const uint32_t rowspace = 3;

static void display(action_t* action);
static int finish(int rc);
static void clear_screen(void);

char* ctext = "yjcv ku vjg pcog qh vjg uauvgo wugf da jco qrgtcvqtu vq ocmg htgg rjqpg ecnnu";
char* stext;

bool use_color;
int style_cipher = A_BOLD;
int style_punct = COLOR_PAIR(CP_WHITE);
int style_soln = COLOR_PAIR(CP_RED);

/**
 * Print a message to the user.
 *
 * This function doesn't actually refresh the display, so make sure
 * you do that once you're ready to display the message.
 */
void
printmsg(const char* msg)
{
	uint32_t mrow, mcol;
	uint32_t msglen = strlen(msg);
	uint32_t msgrow, msgcol;

	getmaxyx(stdscr, mrow, mcol);

	assert(msglen < 3 * mcol);

	msgrow = mrow - (2 + ((msglen + 5) / mcol));
	msgcol = mcol - msglen - 5;

	move(msgrow, msgcol);
	printw("%s", msg);
}

char
rot(char c, uint8_t shift)
{
	return (((c - 'A') + shift) % 26) + 'A';
}

void
solve_caesar(uint32_t stext_idx)
{
	if (stext[stext_idx] == ' ') {
		return;
	}
	int8_t shift = ctext[stext_idx] - toupper(stext[stext_idx]);
	if (shift < 0) {
		shift = 26 + shift;
	}
	shift = 26 - shift;
	uint32_t len = strlen(ctext);

	for (uint32_t i = 0; i < len; ++i) {
		if (isalpha(ctext[i])) {
			stext[i] = rot(ctext[i], (uint8_t)shift);
		}
	}
}

void
sighandler(int sig)
{
	action_t act = {NONE};
	struct winsize ws;

	switch (sig) {
	case SIGWINCH:
		/* Handle screen resize */
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) >= 0) {
			resizeterm(ws.ws_row, ws.ws_col);
		}
		clear_screen();
		display(&act);
		signal(SIGWINCH, sighandler);
		break;
	case SIGINT:
		/* Handle exit (^C) */
		finish(0);
		break;
	}
}

void
clearmsg(void)
{
	uint32_t mrow, mcol;

	getmaxyx(stdscr, mrow, mcol);

	for (uint32_t r = mrow - 5; r < mrow; ++r) {
		for (uint32_t c = 0; c < mcol; ++c) {
			mvaddch(r, c, ' ');
		}
	}
}


static void
clear_screen(void)
{
	uint32_t mrow, mcol;

	getmaxyx(stdscr, mrow, mcol);

	for (uint32_t r = 0; r < mrow; ++r) {
		for (uint32_t c = 0; c < mcol; ++c) {
			mvaddch(r, c, ' ');
		}
	}
}

uint32_t
get_next_space(const char* ctext, uint32_t len, uint32_t pos)
{
	uint32_t i;
	assert(len <= strlen(ctext));
	for (i = pos + 1; i < len; ++i) {
		if (ctext[i] == ' ') {
			return i;
		}
	}
	return i;
}

/*
 * Return a copy of the input ctext, but with words wrapped at rowlen chars
 * by replacing spaces with newlines.
 *
 * mtext -must- be at least as long as ctext
 */
static void
get_row_markers(const char* ctext, uint32_t rowlen, char* mtext)
{
	uint32_t ccol;
	uint32_t tlen = strlen(ctext);
	uint32_t spos = 0, nspos = get_next_space(ctext, tlen, spos);

	memset(mtext, ' ', tlen);
	mtext[tlen] = '\0';

	ccol = 0;
	for (uint32_t i = 0; i < tlen; ++i) {
		if (ctext[i] == ' ') {
			spos = i;
			nspos = get_next_space(ctext, tlen, spos);
			if (ccol + ((nspos - spos)) > rowlen) {
				ccol = 0;
				mtext[i] = '\n';
				++i; /* Carriage return should consume the space */
			}
		}
		mtext[i] = ctext[i];
		++ccol;
	}
}

#define CURSOR_MOUSE UINT32_MAX
#define CURSOR_SET(_c) ((_c) < CURSOR_MOUSE)

static void
display(action_t* action)
{
	static uint32_t cursor_pos = 0;
	uint32_t cursor_row = 0, cursor_col = 0;
	uint32_t crow, ccol;
	uint32_t mrow, mcol;
	uint32_t tlen = strlen(ctext);
	uint32_t spos = 0, nspos = get_next_space(ctext, tlen, spos);
	uint32_t mouserow = 0, mousecol = 0;
	uint32_t old_cursor_pos = cursor_pos;

	clearmsg();

	getmaxyx(stdscr, mrow, mcol);

	old_cursor_pos = cursor_pos;

	/* First, process the action */
	switch (action->act_type) {
	case NONE:
		break;
	case MOVE_CURSOR:
		switch (action->u.direction) {
		case DIR_UP:
		case DIR_DOWN:
		{
			char* mtext = malloc((tlen + 1) * sizeof(*mtext));
			uint32_t mpos = cursor_pos;
			uint32_t cursor_offset = 0;

			get_row_markers(ctext, (mcol - (2 * pad)) / 2, mtext);

			/* First locate the start of the current line */
			while (mpos > 0 && mtext[mpos - 1] != '\n') {
				--mpos;
				++cursor_offset;
			}

			if (action->u.direction == DIR_UP) {
				if (mpos > 0) {
					uint32_t llen = 0;
					/* Find the start of the previous line */
					--mpos;
					while (mpos > 0 && mtext[mpos - 1] != '\n') {
						--mpos;
						++llen;
					}
					if (cursor_offset > llen) {
						cursor_offset = llen;
					}
					while (cursor_offset > 0 && !isalnum(ctext[mpos + cursor_offset])) {
						--cursor_offset;
					}
				}
				assert(mpos + cursor_offset <= cursor_pos);
				cursor_pos = mpos + cursor_offset;
			} else {
				uint32_t llen = 0;
				/* Find the start of the next line */
				++mpos;
				while (mpos < tlen && mtext[mpos - 1] != '\n') {
					++mpos;
				}
				/* Now need to find the start of the subsequent line to prevent overshoot */
				while (mpos + llen < tlen && mtext[mpos + llen] != '\n') {
					++llen;
				}
				if (cursor_offset >= llen) {
					cursor_offset = llen - 1;
				}

				while (mpos + cursor_offset > tlen && cursor_offset > 0) {
					--cursor_offset;
				}
					
				while (mpos + cursor_offset < tlen && !isalnum(ctext[mpos + cursor_offset])) {
					++cursor_offset;
				}
				if (mpos + cursor_offset < tlen) {
					cursor_pos = mpos + cursor_offset;
				}
			}
			free(mtext);
		}
			break;
		case DIR_LEFT:
			if (cursor_pos > 0) {
				do {
					--cursor_pos;
				} while (!isalnum(ctext[cursor_pos]) && cursor_pos > 0);
			}
			break;
		case DIR_RIGHT:
			if (cursor_pos < tlen - 1) {
				do {
					++cursor_pos;
				} while (!isalnum(ctext[cursor_pos]) && cursor_pos < tlen - 1);
			}
			break;
		}
		break;
	case JUMP_CURSOR:
		cursor_pos = CURSOR_MOUSE;
		mouserow = action->u.coords.row;
		mousecol = action->u.coords.col;
		break;
	case ADD_LETTER:
	{
		char pointer = ctext[cursor_pos];

		/* Mark all letters, displaying a message if we find a conflict */
		for (uint32_t i = 0; i < tlen; ++i) {
			if (action->u.letter != ' ' && stext[i] == action->u.letter && ctext[i] != pointer) {
				stext[i] = ' ';
				printmsg("Duplicate letter detected, removing...");
			}
			if (ctext[i] == pointer) {
				stext[i] = action->u.letter;
			}
		}
	}
		break;
	case SOLVE_CAESAR:
		solve_caesar(cursor_pos);
		break;
	}

	/* Do the actual displaying */
	crow = rowspace;
	ccol = pad;
	for (uint32_t i = 0; i < tlen; ++i) {
		if (ctext[i] == ' ') {
			spos = i;
			nspos = get_next_space(ctext, tlen, spos);
			if (ccol + ((nspos - spos) * 2) > mcol - pad) {
				ccol = pad;
				crow += rowspace;
				++i; /* Carriage return should consume the space */
				assert(crow < mrow);
			}
		}
		if (cursor_pos == CURSOR_MOUSE && 
			  isalnum(ctext[i]) && 
			  ccol == mousecol &&
			  (crow == mouserow || crow - 1 == mouserow)) {
			cursor_pos = i;
		}
		
		/* Write the character from ciphertext */
		mvaddch(crow, ccol, ctext[i] | style_cipher);

		/* Write the character from the solution */
		int soln;
		if (isalnum(ctext[i])) {
			if (isalnum(stext[i])) {
				soln = stext[i] | style_soln;
			} else {
				soln = ' ';
			}
		} else {
			soln = ctext[i] | style_punct;
		}
		if (cursor_pos == i) {
			cursor_row = crow - 1;
			cursor_col = ccol;
		}
		mvaddch(crow - 1, ccol, soln);

		ccol += 2;
		assert(ccol <= mcol - pad);
	}

	if (!CURSOR_SET(cursor_pos)) {
		/* User clicked somewhere invalid, now have to redraw everything */
		action->act_type = NONE;
		cursor_pos = old_cursor_pos;
		display(action);
	} else {
		move(cursor_row, cursor_col);
	}
	refresh();
}

static int
finish(int rc)
{
	endwin();
	exit(rc);
	return rc;
}

int
main(int argc, char** argv)
{
	int c = 0;
	action_t act = {NONE};

	/* Prepare the ciphertext and solution */
	if (argc == 1) {
		printf("I need an argument\n");
		return -1;
	} else if (argc > 2) {
		printf("Too many arguments\n");
		return -2;
	} else {
		struct stat st;
		uint32_t fsize;
		int fd = open(argv[1], O_RDONLY);
		if (fd <= 0) {
			printf("Error: %s (%u)\n", strerror(errno), errno);
			return -errno;
		}

		if (stat(argv[1], &st) != 0) {
			printf("Error: %s (%u)\n", strerror(errno), errno);
			return -errno;
		}

		fsize = st.st_size;
		assert(fsize < 1024);

		ctext = malloc((fsize + 1) * sizeof(*ctext));
		assert(ctext != NULL);

		fsize = read(fd, ctext, fsize);
		
		/* Truncate any trailing whitespace */
		for (uint32_t i = fsize; i > 0; --i) {
			if (isspace(ctext[i]) || ctext[i] == '\0') {
				ctext[i] = '\0';
			} else {
				break;
			}
		}
		close(fd);
	}

	stext = malloc((strlen(ctext) + 1) * sizeof(*stext));

	if (signal(SIGWINCH, sighandler) == SIG_ERR) {
		fprintf(stderr, "Signal registration error: SIGWINCH\n");
		return -1;
	}
	if (signal(SIGINT, sighandler) == SIG_ERR) {
		fprintf(stderr, "Signal registration error: SIGINT\n");
		return -1;
	}

	initscr();
	cbreak(); /* Don't buffer until enter */
	noecho();
	keypad(stdscr, true);
	mousemask(BUTTON1_PRESSED, NULL);

	/* Set up colors and color pairs */
	use_color = has_colors();

	if (use_color) {
		start_color();
		init_pair(CP_WHITE, COLOR_WHITE, COLOR_BLACK);
		init_pair(CP_RED, COLOR_RED, COLOR_BLACK);
		init_pair(CP_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
	} else {
		style_cipher = A_BOLD;
		style_soln = 0;
		style_punct = A_DIM;
	}

	move(19, 0);
	printw("ESC twice exits. F2 solves Caesar cypher with currently highlighted letter.");
	while (true) {
		display(&act);
		refresh();
		c = getch();

		if (c == KEY_ESC) {
			break;
		}

		act.act_type = MOVE_CURSOR;
		switch (c) {
		case KEY_UP:
			act.u.direction = DIR_UP;
			break;
		case KEY_DOWN:
			act.u.direction = DIR_DOWN;
			break;
		case KEY_LEFT:
			act.u.direction = DIR_LEFT;
			break;
		case KEY_RIGHT:
			act.u.direction = DIR_RIGHT;
			break;
		case KEY_MOUSE:
		{
			MEVENT me;
			if (getmouse(&me) == OK) {
				act.act_type = JUMP_CURSOR;
				act.u.coords.row = me.y;
				act.u.coords.col = me.x;
			}
		}
			break;
		case KEY_DC:
		case KEY_BACKSPACE:
		case ' ':
			act.act_type = ADD_LETTER;
			act.u.letter = ' ';
			break;
		case KEY_F(2):
			act.act_type = SOLVE_CAESAR;
			break;
		default:
			if (isalnum(c)) {
				act.act_type = ADD_LETTER;
				act.u.letter = c;
			}
			break;
		}
	}

	return finish(0);
}

