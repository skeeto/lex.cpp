%option noyywrap
%%
[a-z]+      printf("(%s)", abctext);
.|\n        /* skip */
%%
int main(void){ abclex(); printf("\n"); return 0; }
