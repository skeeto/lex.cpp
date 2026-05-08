%option noyywrap
%%
foobar      { printf("[FB:%s]", yytext); REJECT; }
foo         { yymore(); printf("[F]"); }
bar         { printf("[B:%s]", yytext); }
.|\n        printf("[%c]", yytext[0]);
%%
int main(void){ yylex(); printf("\n"); return 0; }
