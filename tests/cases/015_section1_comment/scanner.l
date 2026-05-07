%option noyywrap

/* this is a flex-comment in section 1 */

%%
[a-z]+      printf("(%s)", yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
