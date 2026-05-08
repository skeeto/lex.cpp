%option noyywrap
%%
foo  |
bar  { printf("[FB:%s]", yytext); REJECT; }
foo  printf("[F:%s]", yytext);
bar  printf("[B:%s]", yytext);
.|\n /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
