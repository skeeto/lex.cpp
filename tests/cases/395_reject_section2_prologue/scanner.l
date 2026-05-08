%option noyywrap
%%
  /* prologue declares a per-call state visible inside actions */
  static int call_count = 0;
  call_count += 1;
[a-z]+      { printf("[%s:%d]", yytext, call_count); REJECT; }
[a-z]+      { printf("(%s)", yytext); }
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
