%option noyywrap
%%
[a-z]+      printf("(%s)", myxtext);
.|\n        /* skip */
%%
int main(void){ myxlex(); printf("\n"); return 0; }
