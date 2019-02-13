#include "cbor.h"
#include "access/hash.h"

static int	compareCbor(Cbor * a, Cbor * b);
static int	lengthCompareCborText(const struct varlena * a, const struct varlena * b);
static int	cbor_cmp_recursive(CborEntry * a, CborEntry * b, int32 nr, int32 cnt);
static uint32 cbor_hash_recursive(uint32 hash, CborEntry * cbor, int32 nr, int32 cnt);


PG_FUNCTION_INFO_V1(cbor_ne);
Datum
cbor_ne(PG_FUNCTION_ARGS)
{
	Cbor	   *a = PG_GETARG_CBOR(0);
	Cbor	   *b = PG_GETARG_CBOR(1);
	bool		res;

	res = (compareCbor(a, b) != 0);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res);
}

PG_FUNCTION_INFO_V1(cbor_lt);
Datum
cbor_lt(PG_FUNCTION_ARGS)
{
	Cbor	   *a = PG_GETARG_CBOR(0);
	Cbor	   *b = PG_GETARG_CBOR(1);
	bool		res;

	res = (compareCbor(a, b) < 0);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res);
}

PG_FUNCTION_INFO_V1(cbor_gt);
Datum
cbor_gt(PG_FUNCTION_ARGS)
{
	Cbor	   *a = PG_GETARG_CBOR(0);
	Cbor	   *b = PG_GETARG_CBOR(1);
	bool		res;

	res = (compareCbor(a, b) > 0);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res);
}

PG_FUNCTION_INFO_V1(cbor_le);
Datum
cbor_le(PG_FUNCTION_ARGS)
{
	Cbor	   *a = PG_GETARG_CBOR(0);
	Cbor	   *b = PG_GETARG_CBOR(1);
	bool		res;

	res = (compareCbor(a, b) <= 0);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res);
}

PG_FUNCTION_INFO_V1(cbor_ge);
Datum
cbor_ge(PG_FUNCTION_ARGS)
{
	Cbor	   *a = PG_GETARG_CBOR(0);
	Cbor	   *b = PG_GETARG_CBOR(1);
	bool		res;

	res = (compareCbor(a, b) >= 0);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res);
}

PG_FUNCTION_INFO_V1(cbor_eq);
Datum
cbor_eq(PG_FUNCTION_ARGS)
{
	Cbor	   *a = PG_GETARG_CBOR(0);
	Cbor	   *b = PG_GETARG_CBOR(1);
	bool		res;

	res = (compareCbor(a, b) == 0);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res);
}

PG_FUNCTION_INFO_V1(cbor_cmp);
Datum
cbor_cmp(PG_FUNCTION_ARGS)
{
	Cbor	   *a = PG_GETARG_CBOR(0);
	Cbor	   *b = PG_GETARG_CBOR(1);
	int			res;

	res = compareCbor(a, b);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_INT32(res);
}


PG_FUNCTION_INFO_V1(cbor_hash);
Datum
cbor_hash(PG_FUNCTION_ARGS)
{
	Cbor	   *cbor = PG_GETARG_CBOR(0);
	uint32		hash;

	hash = cbor_hash_recursive(0, &cbor->root, 0, 1);

	PG_FREE_IF_COPY(cbor, 0);
	PG_RETURN_INT32(hash);
}


static int
compareCbor(Cbor * a, Cbor * b)
{
	return cbor_cmp_recursive(&a->root, &b->root, 0, 1);
}

static int
cbor_cmp_recursive(CborEntry * a, CborEntry * b, int32 nr, int32 cnt)
{
	int32		i;
	uint32		typeA = a[nr] & CBORENTRY_TYPEMASK;
	uint32		typeB = b[nr] & CBORENTRY_TYPEMASK;

	if (typeA < typeB)
		return -1;
	if (typeA > typeB)
		return 1;

	switch (typeA)
	{
		case CBORENTRY_TYPE_UNSIGNEDINTEGER:
		case CBORENTRY_TYPE_NEGATIVEINTEGER:
			{
				uint64	   *valueA = CBORENTRY_VALUE(a, nr, cnt);
				uint64	   *valueB = CBORENTRY_VALUE(b, nr, cnt);

				if (*valueA < *valueB)
					return -1;
				if (*valueA > *valueB)
					return 1;
				break;
			}

		case CBORENTRY_TYPE_BYTESTRING:
		case CBORENTRY_TYPE_TEXTSTRING:
			{
				return lengthCompareCborText(CBORENTRY_VALUE(a, nr, cnt), CBORENTRY_VALUE(b, nr, cnt));
			}

		case CBORENTRY_TYPE_ARRAY:
			{
				CborContainer *valueA = CBORENTRY_VALUE(a, nr, cnt);
				CborContainer *valueB = CBORENTRY_VALUE(b, nr, cnt);

				if (valueA->count < valueB->count)
					return -1;
				if (valueA->count > valueB->count)
					return 1;
				for (i = 0; i < valueA->count; ++i)
				{
					int			tmp = cbor_cmp_recursive(a, b, i, valueA->count);

					if (tmp)
						return tmp;
				}
				break;
			}

		case CBORENTRY_TYPE_MAP:
			{
				CborContainer *valueA = CBORENTRY_VALUE(a, nr, cnt);
				CborContainer *valueB = CBORENTRY_VALUE(b, nr, cnt);

				if (valueA->count < valueB->count)
					return -1;
				if (valueA->count > valueB->count)
					return 1;
				for (i = 0; i < valueA->count * 2; ++i)
				{
					int			tmp = cbor_cmp_recursive(a, b, i, valueA->count * 2);

					if (tmp)
						return tmp;
				}
				break;
			}

		case CBORENTRY_TYPE_TAG:
			{
				CborTag    *valueA = CBORENTRY_VALUE(a, nr, cnt);
				CborTag    *valueB = CBORENTRY_VALUE(b, nr, cnt);

				if (valueA->value < valueB->value)
					return -1;
				if (valueA->value > valueB->value)
					return 1;
				return cbor_cmp_recursive(a, b, 0, 1);
			}

		case CBORENTRY_TYPE_FLOATORSIMPLE:
			{
				uint64	   *valueA = CBORENTRY_VALUE(a, nr, cnt);
				uint64	   *valueB = CBORENTRY_VALUE(b, nr, cnt);

				if ((*valueA & CBOR_SIMPLEMASK) == CBOR_SIMPLE_VALUE)
				{
					uint8		simpleA = *valueA & 0xFF;
					uint8		simpleB = *valueB & 0xFF;

					if ((*valueB & CBOR_SIMPLEMASK) != CBOR_SIMPLE_VALUE)
						return -1;
					if (simpleA < simpleB)
						return -1;
					if (simpleA > simpleB)
						return 1;
				}
				else
				{
					double		fltA = *((double *) valueA);
					double		fltB = *((double *) valueB);

					if ((*valueB & CBOR_SIMPLEMASK) == CBOR_SIMPLE_VALUE)
						return 1;
					if (fltA < fltB)
						return -1;
					if (fltA > fltB)
						return 1;
				}
				break;
			}
	}

	return 0;
}

static uint32
cbor_hash_recursive(uint32 hash, CborEntry * entry, int32 nr, int32 cnt)
{
	int32		i;
	uint32		type = entry[nr] & CBORENTRY_TYPEMASK;

	hash ^= type;
	hash = (hash << 1) | (hash >> 31);

	switch (type)
	{
		case CBORENTRY_TYPE_UNSIGNEDINTEGER:
		case CBORENTRY_TYPE_NEGATIVEINTEGER:
		case CBORENTRY_TYPE_FLOATORSIMPLE:
			{
				hash ^= DatumGetUInt32(hash_any(CBORENTRY_VALUE(entry, nr, cnt), sizeof(uint64)));
				break;
			}

		case CBORENTRY_TYPE_BYTESTRING:
		case CBORENTRY_TYPE_TEXTSTRING:
			{
				bytea	   *value = CBORENTRY_VALUE(entry, nr, cnt);

				hash ^= DatumGetUInt32(hash_any((unsigned char *) value, VARHDRSZ + VARSIZE(value)));
				break;
			}

		case CBORENTRY_TYPE_ARRAY:
			{
				CborContainer *value = CBORENTRY_VALUE(entry, nr, cnt);

				for (i = 0; i < value->count; ++i)
					hash = cbor_hash_recursive(hash, value->entries, i, value->count);
				break;
			}

		case CBORENTRY_TYPE_MAP:
			{
				CborContainer *value = CBORENTRY_VALUE(entry, nr, cnt);

				for (i = 0; i < value->count * 2; ++i)
					hash = cbor_hash_recursive(hash, value->entries, i, value->count * 2);
				break;
			}

		case CBORENTRY_TYPE_TAG:
			{
				CborTag    *value = CBORENTRY_VALUE(entry, nr, cnt);

				hash ^= DatumGetUInt32(hash_any((unsigned char *) &value->value, sizeof(value->value)));
				hash = cbor_hash_recursive(hash, &value->entry, 0, 1);
				break;
			}
	}

	return hash;
}

static int
lengthCompareCborText(const struct varlena * a, const struct varlena * b)
{
	if (VARSIZE(a) == VARSIZE(b))
		return memcmp(VARDATA(a), VARDATA(b), VARSIZE(a) - VARHDRSZ);
	if (VARSIZE(a) < VARSIZE(b))
		return -1;
	return 1;
}
