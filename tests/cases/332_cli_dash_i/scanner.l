%option noyywrap
%%
hello       printf("[hi]");
world       printf("[wo]");
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
