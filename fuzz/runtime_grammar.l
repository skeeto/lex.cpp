/* Embedded grammar for fuzz_runtime: covers a wide variety of features. */
%option noyywrap yylineno

DIGIT   [0-9]
LETTER  [a-zA-Z_]

%x STR
%x COMMENT

%%

{LETTER}({LETTER}|{DIGIT})*    { /* identifier */ }
{DIGIT}+(\.{DIGIT}+)?          { /* number */ }
"\""                            { BEGIN(STR); }
<STR>"\""                       { BEGIN(INITIAL); }
<STR>\\.                        { /* escape */ }
<STR>[^"\\\n]+                  { /* str body */ }
<STR>\n                         { BEGIN(INITIAL); }
"/*"                            { BEGIN(COMMENT); }
<COMMENT>"*/"                   { BEGIN(INITIAL); }
<COMMENT>.|\n                   { /* skip */ }
^"#".*$                         { /* preprocessor line */ }
[ \t\r\n]+                      { /* whitespace */ }
"=="|"!="|"<="|">="|"&&"|"||"   { /* operators */ }
[+\-*/=<>!&|^%~]                { /* single op */ }
[(){}\[\];,.]                   { /* punct */ }
.                               { /* skip unknown */ }

<<EOF>>                         { return 0; }

%%
/* Driver supplied by fuzz_runtime.cc; do not emit main. */
