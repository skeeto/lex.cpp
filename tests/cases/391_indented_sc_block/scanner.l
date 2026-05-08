%option noyywrap
%x state1
%%
"begin"           { BEGIN(state1); printf("[BEG]"); }
   <state1>{
        [a-z]+    { printf("[W:%s]", yytext); }
        "end"     { BEGIN(INITIAL); printf("[END]"); }
        .         /* skip */
    }
.|\n              /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
