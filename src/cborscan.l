%{
#include "postgres.h"

/* No reason to constrain amount of data slurped */
#define YY_READ_BUF_SIZE 16777216

/* Avoid exit() on fatal scanner errors (a bit ugly -- see yy_fatal_error) */
#undef fprintf
#define fprintf(file, fmt, msg)  fprintf_to_ereport(fmt, msg)

static void
fprintf_to_ereport(const char *fmt, const char *msg)
{
	ereport(ERROR, (errmsg_internal("%s", msg)));
}

/* Handles to the buffer that the lexer uses internally */
static YY_BUFFER_STATE scanbufhandle;
/* this is now declared in cborparse.y: */
/* static char *scanbuf; */
/* static int	scanbuflen; */

/* flex 2.5.4 doesn't bother with a decl for this */
int cbor_yylex(void);

void cbor_scanner_init(const char *str);
void cbor_scanner_finish(void);

static CborValue* newCborValue(CborEntry type);
static CborValue* newCborValueFloat(double value);
static CborValue* newCborValueSimple(uint64 value);
static CborValue* newCborValueByteString(const char* text, int leng);
static CborValue* newCborValueTextString(const char* text, int leng);
static CborValue* newCborValueTextStringAppend(CborValue* value, const char* text, int leng);
static CborValue* newCborValueTextStringAppendUnicode(CborValue* value, uint16 ch);
%}

%option 8bit
%option never-interactive
%option nodefault
%option noinput
%option nounput
%option noyywrap
%option warn
%option prefix="cbor_yy"

%x STR


false        false
true         true
null         null
undefined    undefined
nan          NaN
pinfinity    [+]?Infinity
ninfinity    -Infinity
simple       simple

digit  [0-9]
xdigit [0-9A-Fa-f]
ch     [^\"\\]

integer			{digit}+
pinteger        [+]?{integer}
ninteger        -{integer}
nmax            -18446744073709551616
float           [+-]?({integer}\.)?{integer}([Ee][+-]?{integer})?
bstring         h\'({xdigit}{xdigit})*\'
tstringbegin    \"{ch}*
tstringend      \"

%%


{bstring} { yylval = newCborValueByteString(yytext+2, yyleng-3); return TOK_STRING; }

{tstringbegin} { yylval = newCborValueTextString(yytext+1, yyleng-1); BEGIN(STR); }

<STR>{ch}+ { yylval = newCborValueTextStringAppend(yylval, yytext, yyleng); }
<STR>\\\" { yylval = newCborValueTextStringAppend(yylval, "\"", 1); }
<STR>\\\\ { yylval = newCborValueTextStringAppend(yylval, "\\", 1); }
<STR>\\\/ { yylval = newCborValueTextStringAppend(yylval, "/", 1); }
<STR>\\b { yylval = newCborValueTextStringAppend(yylval, "\b", 1); }
<STR>\\f { yylval = newCborValueTextStringAppend(yylval, "\f", 1); }
<STR>\\n { yylval = newCborValueTextStringAppend(yylval, "\n", 1); }
<STR>\\r { yylval = newCborValueTextStringAppend(yylval, "\r", 1); }
<STR>\\t { yylval = newCborValueTextStringAppend(yylval, "\t", 1); }
<STR>\\u{xdigit}{xdigit}{xdigit}{xdigit} { yylval = newCborValueTextStringAppendUnicode(yylval, strtoul(yytext+2, NULL, 16)); }

<STR>{tstringend} BEGIN(INITIAL); return TOK_STRING;

{pinteger} {
            char *end;
            yylval = newCborValue(CBORENTRY_TYPE_UNSIGNEDINTEGER);
            yylval->value.uint = strtoull(yytext, &end, 10);
            return TOK_PINTEGER;
}
{nmax} {
            yylval = newCborValue(CBORENTRY_TYPE_NEGATIVEINTEGER);
            yylval->value.uint = 0xFFFFFFFFFFFFFFFF;
            return TOK_NINTEGER;
}
{ninteger} {
            char *end;
            yylval = newCborValue(CBORENTRY_TYPE_NEGATIVEINTEGER);
            yylval->value.uint = strtoull(yytext+1, &end, 10) - 1;
            return TOK_NINTEGER;
}
{float} {
            char *end;
            yylval = newCborValueFloat(strtod(yytext, &end));
            return TOK_FLOAT;
}
{false}      yylval = newCborValueSimple(20); return TOK_FLOAT;
{true}       yylval = newCborValueSimple(21); return TOK_FLOAT;
{null}       yylval = newCborValueSimple(22); return TOK_FLOAT;
{undefined}  yylval = newCborValueSimple(23); return TOK_FLOAT;
{nan}        yylval = newCborValueFloat(NAN); return TOK_FLOAT;
{pinfinity}  yylval = newCborValueFloat(+INFINITY); return TOK_FLOAT;
{ninfinity}  yylval = newCborValueFloat(-INFINITY); return TOK_FLOAT;
{simple}     return TOK_SIMPLE;
\[           return TOK_OBRACKET;
\]           return TOK_CBRACKET;
\{           return TOK_OBRACE;
\}           return TOK_CBRACE;
\(           return TOK_OPARENTHESIS;
\)           return TOK_CPARENTHESIS;
\:           return TOK_COLON;
\,           return TOK_COMMA;
[ \t\n\r\f]+ /* discard spaces */
.            return yytext[0]; /* alert parser of the garbage */

%%

void __attribute__((noreturn))
yyerror(Cbor **result, const char *message)
{
	if (*yytext == YY_END_OF_BUFFER_CHAR)
	{
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("bad cbor representation"),
				 /* translator: %s is typically "syntax error" */
				 errdetail("%s at end of input", message)));
	}
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("bad cbor representation"),
				 /* translator: first %s is typically "syntax error" */
				 errdetail("%s at or near \"%s\"", message, yytext)));
	}
}


CborValue* newCborValue(CborEntry type)
{
	CborValue* ret = palloc(sizeof(CborValue));
	ret->type = type;
	return ret;
}

CborValue* newCborValueFloat(double value)
{
	CborValue* ret = newCborValue(CBORENTRY_TYPE_FLOATORSIMPLE);
	ret->value.flt = value;
	return ret;
}

CborValue* newCborValueSimple(uint64 value)
{
	CborValue* ret = newCborValue(CBORENTRY_TYPE_FLOATORSIMPLE);
	ret->value.uint = CBOR_SIMPLE_VALUE | value;
	return ret;
}

static unsigned char xdigittoi(char ch);

unsigned char xdigittoi(char ch)
{
	if (ch <= '9')
		return ch - '0';
	if (ch <= 'F')
		return ch - 'A';
	return ch - 'f';
}

CborValue* newCborValueByteString(const char *text, int leng)
{
	CborValue *ret = palloc(sizeof(CborValue) + leng / 2);
	char *target = (char*) (ret + 1);
	ret->type = CBORENTRY_TYPE_BYTESTRING;
	ret->value.length = leng / 2;

	while (leng) {
		*target++ = xdigittoi(text[0]) << 4 | xdigittoi(text[1]);
		text += 2;
		leng -= 2;
	}

	return ret;
}

CborValue* newCborValueTextString(const char *text, int leng)
{
	CborValue *ret = palloc(sizeof(CborValue) + leng);
	char *target = (char*) (ret + 1);
	ret->type = CBORENTRY_TYPE_TEXTSTRING;
	ret->value.length = leng;
	memcpy(target, text, leng);
	return ret;
}

CborValue* newCborValueTextStringAppend(CborValue* value, const char* text, int leng)
{
	char *target;
	int32 new_leng = value->value.length + leng;
	value = repalloc(value, sizeof(CborValue) + new_leng);
	target = (char*) (value + 1);
	memcpy(target + value->value.length, text, leng);
	value->value.length = new_leng;
	return value;
}

CborValue* newCborValueTextStringAppendUnicode(CborValue* value, uint16 ch)
{
	int len;
	char buffer[5];
	if (ch <= 0x7f) {
		buffer[0] = ch;
		len = 1;
	} else
		len = 0;

	return newCborValueTextStringAppend(value, buffer, len);
}

/*
 * Called before any actual parsing is done
 */
void
cbor_scanner_init(const char *str)
{
	Size	slen = strlen(str);

	/*
	 * Might be left over after ereport()
	 */
	if (YY_CURRENT_BUFFER)
		yy_delete_buffer(YY_CURRENT_BUFFER);

	/*
	 * Make a scan buffer with special termination needed by flex.
	 */
	scanbuflen = slen;
	scanbuf = palloc(slen + 2);
	memcpy(scanbuf, str, slen);
	scanbuf[slen] = scanbuf[slen + 1] = YY_END_OF_BUFFER_CHAR;
	scanbufhandle = yy_scan_buffer(scanbuf, slen + 2);

	BEGIN(INITIAL);
}


/*
 * Called after parsing is done to clean up after cbor_scanner_init()
 */
void
cbor_scanner_finish(void)
{
	yy_delete_buffer(scanbufhandle);
	pfree(scanbuf);
}
