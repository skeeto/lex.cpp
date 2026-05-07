%option noyywrap
%%
"#"         { int c; while ((c = input()) != '\n' && c != 0 && c != EOF) ; printf("<cmt>"); }
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
