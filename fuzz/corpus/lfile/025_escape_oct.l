%option noyywrap
%%
\101        printf("<A>");
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
