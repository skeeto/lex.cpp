%option noyywrap
%%
"X"         { unput('Y'); }
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
