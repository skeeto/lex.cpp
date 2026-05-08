%option noyywrap
%%
    /* this is an indented comment in section 2,
       spanning multiple lines */
[a-z]+      printf("[%s]", yytext);
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
