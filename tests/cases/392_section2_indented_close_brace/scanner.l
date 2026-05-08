%option noyywrap
%%
  /* prologue: indented if-block with closing `}` on its own line */
  static int seen = 0;
  if (1)
    {
      seen += 1;
    }
[a-z]+      printf("[%s,seen=%d]", yytext, seen);
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
