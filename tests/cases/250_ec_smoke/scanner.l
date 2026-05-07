%option noyywrap
%%
[0-9]+      printf("N");
[a-zA-Z_]+  printf("W");
[ \t\n]+    /* skip */
.           printf("?");
%%
int main(void){ yylex(); return 0; }
