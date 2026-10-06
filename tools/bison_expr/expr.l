%option noyywrap nounput noinput never-interactive nounistd

%{
#include <cstdlib>
#include "expr.tab.h"
%}

DIGIT [0-9]

%%

[ \t\r\n]+                         { }
{DIGIT}+("."{DIGIT}+)?([eE][+-]?{DIGIT}+)?  { yylval.d = std::atof(yytext); return NUM; }
[-+*/(),\[\]]                      { return yytext[0]; }
.                                  { return yytext[0]; }

%%
