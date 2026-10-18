/* Experiment 7, version 1: a statement calculator whose statements end with ';'.
 *
 *   x = 3;            assign (26 variables, a-z)
 *   x * 2 + 1;        expression statement: prints its value
 */

%{
#include <stdio.h>

int yylex(void);
void yyerror(const char *msg);

static double vars[26];
%}

%union {
  double num;
  int var;
}

%token <num> NUM
%token <var> VAR
%type <num> expr

%left '+' '-'
%left '*' '/'
%precedence UMINUS

%%

program : %empty
        | program stmt
        ;

stmt    : VAR '=' expr ';'     { vars[$1] = $3; }
        | expr ';'             { printf("%g\n", $1); }
        ;

expr    : NUM                  { $$ = $1; }
        | VAR                  { $$ = vars[$1]; }
        | expr '+' expr        { $$ = $1 + $3; }
        | expr '-' expr        { $$ = $1 - $3; }
        | expr '*' expr        { $$ = $1 * $3; }
        | expr '/' expr        { $$ = $1 / $3; }
        | '-' expr %prec UMINUS { $$ = -$2; }
        | '(' expr ')'         { $$ = $2; }
        ;

%%

void yyerror(const char *msg) { fprintf(stderr, "error: %s\n", msg); }

int main(void) { return yyparse(); }
