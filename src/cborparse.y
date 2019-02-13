%{
#define YYSTYPE CborValue *
#define YYDEBUG 1

#include "postgres.h"
#include "lib/stringinfo.h"

#include "cbor.h"
#include <math.h>

/*
 * Bison doesn't allocate anything that needs to live across parser calls,
 * so we can easily have it use palloc instead of malloc.  This prevents
 * memory leaks if we error out during parsing.  Note this only works with
 * bison >= 2.0.  However, in bison 1.875 the default is to use alloca()
 * if possible, so there's not really much problem anyhow, at least if
 * you're building with gcc.
 */
#define YYMALLOC palloc
#define YYFREE   pfree

extern int cbor_yylex(void);

static char *scanbuf;
static int	scanbuflen;

extern int	cbor_yyparse(Cbor **result);
extern void cbor_yyerror(Cbor **result, const char *message);

static void writeCborValue(StringInfo str, int off, CborEntry* dst, CborValue *value);
static CborValue* newCborValue(CborEntry type);

%}

/* BISON Declarations */
%parse-param {Cbor **result}
%expect 0
%name-prefix="cbor_yy"

%token TOK_PINTEGER TOK_NINTEGER TOK_STRING TOK_FLOAT TOK_SIMPLE TOK_OBRACKET TOK_CBRACKET TOK_OBRACE TOK_CBRACE TOK_OPARENTHESIS TOK_CPARENTHESIS TOK_COLON TOK_COMMA;
%start start

/* Grammar follows */
%%

map: TOK_OBRACE TOK_CBRACE {
		$$ = newCborValue(CBORENTRY_TYPE_MAP);
		$$->value.length = 0;
	}
	| TOK_OBRACE pair_list TOK_CBRACE {
		$$ = $2;
	};

array: TOK_OBRACKET TOK_CBRACKET {
		$$ = newCborValue(CBORENTRY_TYPE_ARRAY);
		$$->value.length = 0;
	}
	 | TOK_OBRACKET value_list TOK_CBRACKET {
	 	$$ = $2;
	 }

pair_list: pair {
		$$ = newCborValue(CBORENTRY_TYPE_MAP);
		$$->value.length = 1;
		$$->child = $1;
	}
	| pair TOK_COMMA pair_list {
		$$ = $3;
		$1->next->next = $$->child;
		$$->value.length += 1;
		$$->child = $1;
	}

pair: value TOK_COLON value {
		$$ = $1;
		$1->next = $3;
		$3->next = NULL;
	}


value_list : value {
		$$ = newCborValue(CBORENTRY_TYPE_ARRAY);
		$$->value.length = 1;
		$$->child = $1;
	}
	| value TOK_COMMA value_list {
		$$ = $3;
		$1->next = $$->child;
		$$->value.length += 1;
		$$->child = $1;
	}

value:
	array { $$ = $1; }
	| map { $$ = $1; }
	| TOK_SIMPLE TOK_OPARENTHESIS TOK_PINTEGER TOK_CPARENTHESIS { $$ = $3; $$->type = CBORENTRY_TYPE_FLOATORSIMPLE; $$->value.uint |= CBOR_SIMPLE_VALUE; }
	| TOK_PINTEGER TOK_OPARENTHESIS value TOK_CPARENTHESIS { $$ = $1; $$->type = CBORENTRY_TYPE_TAG; $$->child = $3; }
	| TOK_PINTEGER { $$ = $1;}
	| TOK_NINTEGER { $$ = $1; }
	| TOK_STRING { $$ = $1; }
	| TOK_FLOAT { $$ = $1; };


start: value {
	StringInfoData buf;
	CborEntry* entry;

	initStringInfo(&buf);
	enlargeStringInfo(&buf, VARHDRSZ + sizeof(CborEntry));
	buf.len = VARHDRSZ + sizeof(CborEntry);
	entry = (CborEntry*)(buf.data + VARHDRSZ);
	writeCborValue(&buf, buf.len, entry, $1);

	SET_VARSIZE(buf.data, buf.len);
	*result = (Cbor*) buf.data;
}


%%



void writeCborValue(StringInfo str, int off, CborEntry* dst, CborValue *value)
{
	int32 i;
	int requiredSize = 0;
	void *data;

	data = str->data + str->len;

	switch (value->type)
	{
	case CBORENTRY_TYPE_UNSIGNEDINTEGER:
	case CBORENTRY_TYPE_NEGATIVEINTEGER:
		requiredSize += sizeof(value->value.uint);
		break;

	case CBORENTRY_TYPE_FLOATORSIMPLE:
		requiredSize += sizeof(value->value.flt);
		break;

	case CBORENTRY_TYPE_BYTESTRING:
	case CBORENTRY_TYPE_TEXTSTRING:
		requiredSize += INTALIGN(VARHDRSZ + value->value.length);
		break;

	case CBORENTRY_TYPE_TAG:
		requiredSize += sizeof(uint64) + sizeof(CborEntry);
		break;

	case CBORENTRY_TYPE_ARRAY:
		requiredSize += sizeof(int32) + value->value.length * sizeof(CborEntry);
		break;

	case CBORENTRY_TYPE_MAP:
		requiredSize += sizeof(int32) + value->value.length * sizeof(CborEntry) * 2;
		break;
	}

	enlargeStringInfo(str, requiredSize);
	str->len += requiredSize;

	switch (value->type)
	{
	case CBORENTRY_TYPE_UNSIGNEDINTEGER:
	case CBORENTRY_TYPE_NEGATIVEINTEGER:
		*((uint64*)data) = value->value.uint;
		break;

	case CBORENTRY_TYPE_FLOATORSIMPLE:
		*((double*)data) = value->value.flt;
		break;

	case CBORENTRY_TYPE_BYTESTRING:
	case CBORENTRY_TYPE_TEXTSTRING:
	{
		char *target = VARDATA(data);
		SET_VARSIZE(data, value->value.length + VARHDRSZ);
		memcpy(target, value + 1, value->value.length);
		for (i = 0; i < INTALIGN(VARHDRSZ + value->value.length) - VARHDRSZ - value->value.length; ++i)
			target[value->value.length + i] = 0;
		break;
	}

	case CBORENTRY_TYPE_TAG:
	{
		CborTag* tag = data;
		tag->value = value->value.uint;
		writeCborValue(str, str->len, &tag->entry, value->child);
		break;
	}

	case CBORENTRY_TYPE_ARRAY:
	{
		CborContainer* container = data;
		CborValue* entr = value->child;
		int len = str->len;
		container->count = value->value.length;
		for (i = 0; i < value->value.length; ++i) {
			CborValue *next = entr->next;
			writeCborValue(str, len, container->entries + i, entr);
			entr = next;
		}
		break;
	}

	case CBORENTRY_TYPE_MAP:
	{
		CborContainer* container = data;
		CborValue* entr = value->child;
		int len = str->len;
		container->count = value->value.length;
		for (i = 0; i < value->value.length * 2; ++i) {
			CborValue *next = entr->next;
			writeCborValue(str, len, container->entries + i, entr);
			entr = next;
		}
		break;
	}
	}

	*dst = value->type | (str->len - off);
	pfree(value);
}

#include "cborscan.c"
