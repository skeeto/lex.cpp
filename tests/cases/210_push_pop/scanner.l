%option noyywrap stack
%x COMMENT
%%
"/*"                { yy_push_state(COMMENT); }
<COMMENT>"/*"       { yy_push_state(COMMENT); }
<COMMENT>"*/"       { yy_pop_state(); }
<COMMENT>.|\n       /* skip */
[a-z]+              printf("(%s)", yytext);
.|\n                /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
