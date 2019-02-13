#ifndef __CBOR_H__
#define __CBOR_H__

#include "postgres.h"

typedef uint32 CborEntry;

#define CBORENTRY_POSMASK 0x1FFFFFFF
#define CBORENTRY_TYPEMASK 0xE0000000

#define CBORENTRY_TYPE_UNSIGNEDINTEGER 0x00000000
#define CBORENTRY_TYPE_NEGATIVEINTEGER 0x20000000
#define CBORENTRY_TYPE_BYTESTRING 0x40000000
#define CBORENTRY_TYPE_TEXTSTRING 0x60000000
#define CBORENTRY_TYPE_ARRAY 0x80000000
#define CBORENTRY_TYPE_MAP 0xA0000000
#define CBORENTRY_TYPE_TAG 0xC0000000
#define CBORENTRY_TYPE_FLOATORSIMPLE 0xE0000000

#define CBORENTRY_ENDPOS(ce_, i) ((ce_)[i] & CBORENTRY_POSMASK)
#define CBORENTRY_OFF(ce_, i) ((i) == 0 ? 0 : CBORENTRY_ENDPOS(ce_, i-1))
#define CBORENTRY_VALUE(ce_, i, cnt) ((void*)((char*)((ce_) + cnt) + CBORENTRY_OFF(ce_, i)))

#define CBORENTRY_GETSTR(ce_, i, cnt) (((char*) CBORENTRY_VALUE(ce_, i, cnt)) + VARHDRSZ)
#define CBORENTRY_STRLEN(ce_, i, cnt) (VARSIZE(CBORENTRY_VALUE(ce_, i, cnt)) - VARHDRSZ)
#define CBORENTRY_SETSTRLEN(ce_, i, cnt, len) SET_VARSIZE(CBORENTRY_VALUE(ce_, i, cnt), VARHDRSZ + len)

#define CBOR_SIMPLE_VALUE 0x7FFFFFFFFFFFFF00
#define CBOR_SIMPLEMASK   0xFFFFFFFFFFFFFF00

#define CBORENTRY_INDEFINITE 0x1F
#define CBORENTRY_BREAK 0xFF

typedef struct CborContainer
{
	int32		count;
	CborEntry	entries[1];
}	CborContainer;

typedef struct CborTag
{
	uint64		value;
	CborEntry	entry;
}	CborTag;

typedef struct Cbor
{
	/* varlena header (do not touch directly!) */
	int32		vl_len_;
	CborEntry	root;
}	Cbor;

typedef struct CborValue
{
	CborEntry	type;

	union
	{
		uint64		uint;
		double		flt;
		int32		length;
	}			value;

	struct CborValue *child;
	struct CborValue *next;

}	CborValue;


#define DatumGetCbor(x) ((Cbor*)DatumGetPointer(x))
#define PG_GETARG_CBOR(x)	DatumGetCbor(PG_DETOAST_DATUM(PG_GETARG_DATUM(x)) )
#define PG_RETURN_CBOR(x)	PG_RETURN_POINTER(x)

#endif
