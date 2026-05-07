%%
[a-z]+      printf("[%s]", yytext);
.|\n        ECHO;
%%
int main(void) { yylex(); printf("\n"); return 0; }
