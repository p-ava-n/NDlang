/* Experiment 7, version 2: the calculator from calc_semi.y with the statement
 * terminator changed from ';' to a newline.
 *
 *   x = 3             assign (26 variables, a-z)
 *   x * 2 + 1         expression statement: prints its value
 *
 * Changes from calc_semi.y: the new EOL token replaces ';' in both statement
 * rules, and a blank line (a lone EOL) is now an empty statement.
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
%token EOL
%type <num> expr

%left '+' '-'
%left '*' '/'
%precedence UMINUS

%%

program : %empty
        | program stmt
        ;

stmt    : VAR '=' expr EOL     { vars[$1] = $3; }
        | expr EOL             { printf("%g\n", $1); }
        | EOL                  { /* blank line */ }
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
