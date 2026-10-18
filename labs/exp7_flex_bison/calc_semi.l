/* Experiment 7, version 1: statements end with ';'.
 * Newlines are ordinary whitespace, so one statement may span several lines. */

%option noyywrap nounput noinput

%{
#include <stdlib.h>
#include "calc_semi.tab.h"
%}

%%

[0-9]+("."[0-9]+)?   { yylval.num = atof(yytext); return NUM; }
[a-z]                { yylval.var = yytext[0] - 'a'; return VAR; }
[ \t\r\n]+           { /* whitespace, including newlines */ }
.                    { return yytext[0]; /* operators, '(' ')' '=' and ';' */ }

%%
