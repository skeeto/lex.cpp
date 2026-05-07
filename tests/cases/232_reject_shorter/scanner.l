%option noyywrap
%%
"abcdef"    { printf("[LONG]"); REJECT; }
"abc"       { printf("[SHORT:%s]", yytext); }
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
