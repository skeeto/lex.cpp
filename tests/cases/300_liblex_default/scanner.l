%%
[a-z]+      printf("(%s)", yytext);
[ \t\n]+    /* skip */
.           printf("?");
%%
