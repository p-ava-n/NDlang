/* NDLang scanner. Flex generates the matching code; src/Lexer.cpp wraps it. */

%option noyywrap nounput noinput never-interactive nounistd

%{
#include <cstdio>
#include "ndlang/Token.h"

using namespace ndlang;

int ndl_tok_line = 1;
int ndl_tok_col = 1;
static int cur_line = 1;
static int cur_col = 1;

#define YY_USER_ACTION  ndl_tok_line = cur_line; ndl_tok_col = cur_col; cur_col += yyleng;
%}

DIGIT   [0-9]
ID      [A-Za-z_][A-Za-z0-9_]*

%%

[ \t\r]+                { /* skip whitespace */ }
\n                      { cur_line++; cur_col = 1; }
"//".*                  { /* line comment */ }

"func"                  { return KW_FUNC; }
"let"                   { return KW_LET; }
"return"                { return KW_RETURN; }
"if"                    { return KW_IF; }
"else"                  { return KW_ELSE; }
"for"                   { return KW_FOR; }
"in"                    { return KW_IN; }
"true"                  { return KW_TRUE; }
"false"                 { return KW_FALSE; }
"int"                   { return KW_INT; }
"float"                 { return KW_FLOAT; }
"bool"                  { return KW_BOOL; }
"vec"                   { return KW_VEC; }

{DIGIT}+"."{DIGIT}+([eE][+-]?{DIGIT}+)?  { return FLOAT_LIT; }
{DIGIT}+                { return INT_LIT; }
{ID}                    { return IDENT; }

"->"                    { return ARROW; }
".."                    { return DOTDOT; }
"<="                    { return LE; }
">="                    { return GE; }
"=="                    { return EQ; }
"!="                    { return NE; }
"&&"                    { return AND; }
"||"                    { return OR; }
"("                     { return LPAREN; }
")"                     { return RPAREN; }
"{"                     { return LBRACE; }
"}"                     { return RBRACE; }
"["                     { return LBRACKET; }
"]"                     { return RBRACKET; }
","                     { return COMMA; }
":"                     { return COLON; }
";"                     { return SEMI; }
"="                     { return ASSIGN; }
"+"                     { return PLUS; }
"-"                     { return MINUS; }
"*"                     { return STAR; }
"/"                     { return SLASH; }
"<"                     { return LT; }
">"                     { return GT; }
"!"                     { return NOT; }

.                       { return T_ERROR; }

%%

void ndl_scan_begin(const char *src) {
  cur_line = 1;
  cur_col = 1;
  yy_scan_string(src);
}

void ndl_scan_end(void) { yy_delete_buffer(YY_CURRENT_BUFFER); }
