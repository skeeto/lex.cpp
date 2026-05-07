%option noyywrap
%{
static int col = 1;
#define YY_USER_ACTION { printf("[c%d]", col); col += yyleng; }
%}
%%
[a-z]+      printf("(%s)", yytext);
\n          { col = 1; }
.           /* skip */
%%
int main(void){ yylex(); return 0; }
