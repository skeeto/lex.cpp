%option noyywrap
%x STR
%%
\"           { BEGIN(STR); }
<STR>[a-z]+  { yymore(); }
<STR>" "     { yymore(); }
<STR>\"      { BEGIN(INITIAL); printf("[STR=<%s>]", yytext); }
.|\n         /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
