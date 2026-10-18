# Standalone build for the Experiment 7 warm-up (the main CMake build also
# builds and tests these as calc_semi and calc_nl).
#
#   make            build both calculators
#   make demo       run the same program through each one
#   make diff       show exactly what changed between the two versions

CC = gcc

all: calc_semi calc_nl

calc_semi: calc_semi.y calc_semi.l
	bison -d -o calc_semi.tab.c calc_semi.y
	flex -o calc_semi.lex.c calc_semi.l
	$(CC) -o $@ calc_semi.tab.c calc_semi.lex.c

calc_nl: calc_nl.y calc_nl.l
	bison -d -o calc_nl.tab.c calc_nl.y
	flex -o calc_nl.lex.c calc_nl.l
	$(CC) -o $@ calc_nl.tab.c calc_nl.lex.c

demo: all
	printf 'x = 3;\ny = x * 2 + 1;\ny;\n' | ./calc_semi
	printf 'x = 3\ny = x * 2 + 1\ny\n' | ./calc_nl

diff:
	-diff -u calc_semi.l calc_nl.l
	-diff -u calc_semi.y calc_nl.y

clean:
	rm -f calc_semi calc_nl *.tab.c *.tab.h *.lex.c

.PHONY: all demo diff clean
