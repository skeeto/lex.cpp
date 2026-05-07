%option noyywrap
%%
"abc"       printf("[ABC]");
"xyz"       printf("[XYZ]");
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
