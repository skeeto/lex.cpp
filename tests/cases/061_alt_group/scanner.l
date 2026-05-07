%option noyywrap
%%
(ab|cd)+    printf("[%s]", yytext);
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
