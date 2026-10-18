/* Experiment 7, version 2: statements end with a newline instead of ';'.
 * Changes from calc_semi.l: '\n' is no longer whitespace but the EOL token,
 * and ';' is no longer part of the language. */

%option noyywrap nounput noinput

%{
#include <stdlib.h>
#include "calc_nl.tab.h"
%}

%%

[0-9]+("."[0-9]+)?   { yylval.num = atof(yytext); return NUM; }
[a-z]                { yylval.var = yytext[0] - 'a'; return VAR; }
[ \t\r]+             { /* whitespace, newlines excluded */ }
\n                   { return EOL; }
.                    { return yytext[0]; /* operators, '(' ')' '=' */ }

%%
