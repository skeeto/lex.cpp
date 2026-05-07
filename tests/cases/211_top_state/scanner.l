%option noyywrap stack
%x A B
%%
"<A>"           { yy_push_state(A); }
"<B>"           { yy_push_state(B); }
"<.>"           { printf("[top=%d]", yy_top_state()); }
"</>"           { yy_pop_state(); }
[a-z]+          /* skip */
.|\n            /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
