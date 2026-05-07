%%
[a-z]+      printf("(%s)", yytext);
[0-9]+      printf("[%s]", yytext);
.|\n        /* skip */
%%
