#include "cbor.h"
#include "access/hash.h"
#include "catalog/pg_type.h"
#include "utils/array.h"
#include "utils/builtins.h"

static int	compareCbor(Cbor * a, Cbor * b);
static int	lengthCompareCborText(const struct varlena * a, const struct varlena * b);
static int	cbor_cmp_recursive(CborEntry * a, CborEntry * b, int32 nr, int32 cnt);
static uint32 cbor_hash_recursive(uint32 hash, CborEntry * cbor, int32 nr, int32 cnt);
static Cbor *cbor_from_entry(CborEntry * entry, int32 nr, int32 cnt);


PG_FUNCTION_INFO_V1(cbor_object_field);
Datum
cbor_object_field(PG_FUNCTION_ARGS)
{
	Cbor	   *cbor = PG_GETARG_CBOR(0);
	text	   *key = PG_GETARG_TEXT_PP(1);
	CborContainer *container = CBORENTRY_VALUE(&cbor->root, 0, 1);
	int32		i;

	if ((cbor->root & CBORENTRY_TYPEMASK) != CBORENTRY_TYPE_MAP)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot call %s on value of major type %d",
					 "cbor_object_field (cbor -> text)", cbor->root >> 29)));

	for (i = 0; i < container->count; ++i)
	{
		text	   *k = CBORENTRY_VALUE(container->entries, i * 2, container->count * 2);

		if ((container->entries[i * 2] & CBORENTRY_TYPEMASK) != CBORENTRY_TYPE_TEXTSTRING)
			continue;

		if (!lengthCompareCborText(k, key))
			PG_RETURN_CBOR(cbor_from_entry(container->entries, i * 2 + 1, container->count * 2));
	}

	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(cbor_array_element);
Datum
cbor_array_element(PG_FUNCTION_ARGS)
{
	Cbor	   *cbor = PG_GETARG_CBOR(0);
	int32		nr = PG_GETARG_INT32(1);
	CborContainer *container = CBORENTRY_VALUE(&cbor->root, 0, 1);

	if ((cbor->root & CBORENTRY_TYPEMASK) != CBORENTRY_TYPE_ARRAY)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot call %s on value of major type %d",
					 "cbor_array_element (cbor -> int)", cbor->root >> 29)));

	if (nr < container->count)
		PG_RETURN_CBOR(cbor_from_entry(container->entries, nr, container->count));

	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(cbor_extract_path);
Datum
cbor_extract_path(PG_FUNCTION_ARGS)
{
	Cbor	   *cbor = PG_GETARG_CBOR(0);
	ArrayType  *path = PG_GETARG_ARRAYTYPE_P(1);
	CborEntry  *entry = &cbor->root;
	Datum	   *pathtext;
	bool	   *pathnulls;
	int			npath;
	int			i;
	int32		index = 0;
	int32		last_cnt = 1;

	if (array_contains_nulls(path))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot call %s with null path elements",
						"cbor_extract_path (cbor -> text[])")));

	deconstruct_array(path, TEXTOID, -1, false, 'i',
					  &pathtext, &pathnulls, &npath);

	for (i = 0; i < npath; i++)
	{
		CborContainer *container = CBORENTRY_VALUE(entry, index, last_cnt);

		if ((entry[index] & CBORENTRY_TYPEMASK) == CBORENTRY_TYPE_ARRAY)
		{
			long		lindex;
			char	   *indextext = TextDatumGetCString(pathtext[i]);
			char	   *endptr;

			lindex = strtol(indextext, &endptr, 10);
			if (*endptr != '\0' || lindex >= container->count || lindex < 0)
				PG_RETURN_NULL();
			index = (int32) lindex;
			last_cnt = container->count;
			entry = container->entries;
		}
		else if ((entry[index] & CBORENTRY_TYPEMASK) == CBORENTRY_TYPE_MAP)
		{
			int32		j;

			index = -1;

			for (j = 0; j < container->count; ++j)
			{
				text	   *k = CBORENTRY_VALUE(container->entries, j * 2, container->count * 2);

				if ((container->entries[j * 2] & CBORENTRY_TYPEMASK) != CBORENTRY_TYPE_TEXTSTRING)
					continue;

				if (!lengthCompareCborText(k, (const text *) pathtext[i]))
				{
					index = j * 2 + 1;
					last_cnt = container->count * 2;
					entry = container->entries;
					break;
				}
			}

			if (index < 0)
				PG_RETURN_NULL();
		}
		else
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("cannot call %s on value of major type %d",
					   "cbor_extract_path (cbor -> text[])", *entry >> 29)));
	}

	PG_RETURN_CBOR(cbor_from_entry(entry, index, last_cnt));
}

PG_FUNCTION_INFO_V1(cbor_exists);
Datum
cbor_exists(PG_FUNCTION_ARGS)
{
	Cbor	   *cbor = PG_GETARG_CBOR(0);
	text	   *key = PG_GETARG_TEXT_PP(1);
	CborContainer *container = CBORENTRY_VALUE(&cbor->root, 0, 1);
	int32		i;

	if ((cbor->root & CBORENTRY_TYPEMASK) != CBORENTRY_TYPE_MAP)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot call %s on value of major type %d",
						"cbor_exists (cbor -> text)", cbor->root >> 29)));

	for (i = 0; i < container->count; ++i)
	{
		text	   *k = CBORENTRY_VALUE(container->entries, i * 2, container->count * 2);

		if ((container->entries[i * 2] & CBORENTRY_TYPEMASK) != CBORENTRY_TYPE_TEXTSTRING)
			continue;

		if (!lengthCompareCborText(k, key))
			PG_RETURN_BOOL(true);
	}

	PG_RETURN_BOOL(false);
}

PG_FUNCTION_INFO_V1(cbor_exists_any);
Datum
cbor_exists_any(PG_FUNCTION_ARGS)
{
	Cbor	   *cbor = PG_GETARG_CBOR(0);
	ArrayType  *keys = PG_GETARG_ARRAYTYPE_P(1);
	text	   *key = PG_GETARG_TEXT_PP(1);
	CborContainer *container = CBORENTRY_VALUE(&cbor->root, 0, 1);
	int			i;
	int32		j;
	Datum	   *key_datums;
	bool	   *key_nulls;
	int			elem_count;

	deconstruct_array(keys, TEXTOID, -1, false, 'i', &key_datums, &key_nulls,
					  &elem_count);

	if ((cbor->root & CBORENTRY_TYPEMASK) != CBORENTRY_TYPE_MAP)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot call %s on value of major type %d",
						"cbor_exists_any (cbor -> text)", cbor->root >> 29)));

	for (i = 0; i < elem_count; i++)
	{
		for (j = 0; j < container->count; ++j)
		{
			text	   *k = CBORENTRY_VALUE(container->entries, j * 2, container->count * 2);

			if ((container->entries[j * 2] & CBORENTRY_TYPEMASK) != CBORENTRY_TYPE_TEXTSTRING)
				continue;

			if (!lengthCompareCborText(k, key))
				PG_RETURN_BOOL(true);
		}
	}

	PG_RETURN_BOOL(false);
}

PG_FUNCTION_INFO_V1(cbor_exists_all);
Datum
cbor_exists_all(PG_FUNCTION_ARGS)
{
	Cbor	   *cbor = PG_GETARG_CBOR(0);
	ArrayType  *keys = PG_GETARG_ARRAYTYPE_P(1);
	text	   *key = PG_GETARG_TEXT_PP(1);
	CborContainer *container = CBORENTRY_VALUE(&cbor->root, 0, 1);
	int			i;
	int32		j;
	Datum	   *key_datums;
	bool	   *key_nulls;
	int			elem_count;

	deconstruct_array(keys, TEXTOID, -1, false, 'i', &key_datums, &key_nulls,
					  &elem_count);

	if ((cbor->root & CBORENTRY_TYPEMASK) != CBORENTRY_TYPE_MAP)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot call %s on value of major type %d",
						"cbor_exists_any (cbor -> text)", cbor->root >> 29)));

	for (i = 0; i < elem_count; i++)
	{
		bool		found = false;

		for (j = 0; j < container->count; ++j)
		{
			text	   *k = CBORENTRY_VALUE(container->entries, j * 2, container->count * 2);

			if ((container->entries[j * 2] & CBORENTRY_TYPEMASK) != CBORENTRY_TYPE_TEXTSTRING)
				continue;

			if (!lengthCompareCborText(k, key))
			{
				found = true;
				break;
			}
		}
		if (!found)
			PG_RETURN_BOOL(false);
	}

	PG_RETURN_BOOL(true);
}

PG_FUNCTION_INFO_V1(cbor_contains);
Datum
cbor_contains(PG_FUNCTION_ARGS)
{
	Cbor	   *a = PG_GETARG_CBOR(0);
	Cbor	   *b = PG_GETARG_CBOR(1);

	(void) a;
	(void) b;

	PG_RETURN_BOOL(false);
}

PG_FUNCTION_INFO_V1(cbor_contained);
Datum
cbor_contained(PG_FUNCTION_ARGS)
{
	/* Commutator of "contains" */
	Cbor	   *a = PG_GETARG_CBOR(0);
	Cbor	   *b = PG_GETARG_CBOR(1);

	(void) a;
	(void) b;

	PG_RETURN_BOOL(false);
}


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

static Cbor *
cbor_from_entry(CborEntry * entry, int32 nr, int32 cnt)
{
	StringInfoData buf;
	int32		len = CBORENTRY_ENDPOS(entry, nr) - CBORENTRY_OFF(entry, nr);
	CborEntry	root = (entry[nr] & CBORENTRY_TYPEMASK) | len;

	initStringInfo(&buf);
	enlargeStringInfo(&buf, VARHDRSZ);
	buf.len = VARHDRSZ;
	appendBinaryStringInfo(&buf, (const char *) &root, sizeof(root));
	appendBinaryStringInfo(&buf, CBORENTRY_VALUE(entry, nr, cnt), len);

	SET_VARSIZE(buf.data, buf.len);
	return (Cbor *) buf.data;
}


#if 0

PG_FUNCTION_INFO_V1(cbor_contains);
Datum
cbor_exists(PG_FUNCTION_ARGS)
{
}

Datum		cbor_exists_any(PG_FUNCTION_ARGS);
Datum		cbor_exists_all(PG_FUNCTION_ARGS);
Datum		cbor_contains(PG_FUNCTION_ARGS);
Datum		cbor_contained(PG_FUNCTION_ARGS);
Datum		cbor_ne(PG_FUNCTION_ARGS);
Datum		cbor_lt(PG_FUNCTION_ARGS);
Datum		cbor_gt(PG_FUNCTION_ARGS);
Datum		cbor_le(PG_FUNCTION_ARGS);
Datum		cbor_ge(PG_FUNCTION_ARGS);
Datum		cbor_eq(PG_FUNCTION_ARGS);
Datum		cbor_cmp(PG_FUNCTION_ARGS);
Datum		cbor_hash(PG_FUNCTION_ARGS);


/*
 ** R-tree support functions
 */

PG_FUNCTION_INFO_V1(cbor_contains);
PG_FUNCTION_INFO_V1(cbor_contained);
PG_FUNCTION_INFO_V1(cbor_overlap);
PG_FUNCTION_INFO_V1(cbor_union);
PG_FUNCTION_INFO_V1(cbor_inter);
PG_FUNCTION_INFO_V1(cbor_size);

/*
 ** miscellaneous
 */
PG_FUNCTION_INFO_V1(cbor_distance);
PG_FUNCTION_INFO_V1(cbor_is_point);
PG_FUNCTION_INFO_V1(cbor_enlarge);

/*
 ** For internal use only
 */
int32		cbor_cmp_v0(NDBOX *a, NDBOX *b);
bool		cbor_contains_v0(NDBOX *a, NDBOX *b);
bool		cbor_overlap_v0(NDBOX *a, NDBOX *b);
NDBOX	   *cbor_union_v0(NDBOX *a, NDBOX *b);
void		rt_cbor_size(NDBOX *a, double *sz);
NDBOX	   *g_cbor_binary_union(NDBOX *r1, NDBOX *r2, int *sizep);
bool		g_cbor_leaf_consistent(NDBOX *key, NDBOX *query, StrategyNumber strategy);
bool g_cbor_internal_consistent(NDBOX *key, NDBOX *query,
						   StrategyNumber strategy);

/*
 ** Auxiliary funxtions
 */
static double distance_1D(double a1, double a2, double b1, double b2);
static bool cbor_is_point_internal(NDBOX *cbor);

/*****************************************************************************
 * Input/Output functions
 *****************************************************************************/

/*
 ** Allows the construction of a cbor from 2 float[]'s
 */
Datum
cbor_a_f8_f8(PG_FUNCTION_ARGS)
{
	ArrayType  *ur = PG_GETARG_ARRAYTYPE_P(0);
	ArrayType  *ll = PG_GETARG_ARRAYTYPE_P(1);
	NDBOX	   *result;
	int			i;
	int			dim;
	int			size;
	bool		point;
	double	   *dur,
			   *dll;

	if (array_contains_nulls(ur) || array_contains_nulls(ll))
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_ELEMENT_ERROR), errmsg("cannot work with arrays containing NULLs")));

	dim = ARRNELEMS(ur);
	if (ARRNELEMS(ll) != dim)
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_ELEMENT_ERROR), errmsg("UR and LL arrays must be of same length")));

	dur = ARRPTR(ur);
	dll = ARRPTR(ll);

	/* Check if it's a point */
	point = true;
	for (i = 0; i < dim; i++)
	{
		if (dur[i] != dll[i])
		{
			point = false;
			break;
		}
	}

	size = point ? POINT_SIZE(dim) : CUBE_SIZE(dim);
	result = (NDBOX *) palloc0(size);
	SET_VARSIZE(result, size);
	SET_DIM(result, dim);

	for (i = 0; i < dim; i++)
		result->x[i] = dur[i];

	if (!point)
	{
		for (i = 0; i < dim; i++)
			result->x[i + dim] = dll[i];
	}
	else
		SET_POINT_BIT(result);

	PG_RETURN_NDBOX(result);
}

/*
 ** Allows the construction of a zero-volume cbor from a float[]
 */
Datum
cbor_a_f8(PG_FUNCTION_ARGS)
{
	ArrayType  *ur = PG_GETARG_ARRAYTYPE_P(0);
	NDBOX	   *result;
	int			i;
	int			dim;
	int			size;
	double	   *dur;

	if (array_contains_nulls(ur))
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_ELEMENT_ERROR), errmsg("cannot work with arrays containing NULLs")));

	dim = ARRNELEMS(ur);

	dur = ARRPTR(ur);

	size = POINT_SIZE(dim);
	result = (NDBOX *) palloc0(size);
	SET_VARSIZE(result, size);
	SET_DIM(result, dim);
	SET_POINT_BIT(result);

	for (i = 0; i < dim; i++)
		result->x[i] = dur[i];

	PG_RETURN_NDBOX(result);
}

Datum
cbor_subset(PG_FUNCTION_ARGS)
{
	NDBOX	   *c = PG_GETARG_NDBOX(0);
	ArrayType  *idx = PG_GETARG_ARRAYTYPE_P(1);
	NDBOX	   *result;
	int			size,
				dim,
				i;
	int		   *dx;

	if (array_contains_nulls(idx))
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_ELEMENT_ERROR), errmsg("cannot work with arrays containing NULLs")));

	dx = (int32 *) ARR_DATA_PTR(idx);

	dim = ARRNELEMS(idx);
	size = IS_POINT(c) ? POINT_SIZE(dim) : CUBE_SIZE(dim);
	result = (NDBOX *) palloc0(size);
	SET_VARSIZE(result, size);
	SET_DIM(result, dim);

	if (IS_POINT(c))
		SET_POINT_BIT(result);

	for (i = 0; i < dim; i++)
	{
		if ((dx[i] <= 0) || (dx[i] > DIM(c)))
		{
			pfree(result);
			ereport(ERROR,
					(errcode(ERRCODE_ARRAY_ELEMENT_ERROR), errmsg("Index out of bounds")));
		}
		result->x[i] = c->x[dx[i] - 1];
		if (!IS_POINT(c))
			result->x[i + dim] = c->x[dx[i] + DIM(c) - 1];
	}

	PG_FREE_IF_COPY(c, 0);
	PG_RETURN_NDBOX(result);
}

/*****************************************************************************
 *						   GiST functions
 *****************************************************************************/

/*
 ** The GiST Consistent method for boxes
 ** Should return false if for all data items x below entry,
 ** the predicate x op query == FALSE, where op is the oper
 ** corresponding to strategy in the pg_amop table.
 */
Datum
g_cbor_consistent(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	NDBOX	   *query = PG_GETARG_NDBOX(1);
	StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);

	/* Oid		subtype = PG_GETARG_OID(3); */
	bool	   *recheck = (bool *) PG_GETARG_POINTER(4);
	bool		res;

	/* All cases served by this function are exact */
	*recheck = false;

	/*
	 * if entry is not leaf, use g_cbor_internal_consistent, else use
	 * g_cbor_leaf_consistent
	 */
	if (GIST_LEAF(entry))
		res = g_cbor_leaf_consistent(DatumGetNDBOX(entry->key), query,
									 strategy);
	else
		res = g_cbor_internal_consistent(DatumGetNDBOX(entry->key), query,
										 strategy);

	PG_FREE_IF_COPY(query, 1);
	PG_RETURN_BOOL(res);
}

/*
 ** The GiST Union method for boxes
 ** returns the minimal bounding box that encloses all the entries in entryvec
 */
Datum
g_cbor_union(PG_FUNCTION_ARGS)
{
	GistEntryVector *entryvec = (GistEntryVector *) PG_GETARG_POINTER(0);
	int		   *sizep = (int *) PG_GETARG_POINTER(1);
	NDBOX	   *out = (NDBOX *) NULL;
	NDBOX	   *tmp;
	int			i;

	/*
	 * fprintf(stderr, "union\n");
	 */
	tmp = DatumGetNDBOX(entryvec->vector[0].key);

	/*
	 * sizep = sizeof(NDBOX); -- NDBOX has variable size
	 */
	*sizep = VARSIZE(tmp);

	for (i = 1; i < entryvec->n; i++)
	{
		out = g_cbor_binary_union(tmp, DatumGetNDBOX(entryvec->vector[i].key),
								  sizep);
		tmp = out;
	}

	PG_RETURN_POINTER(out);
}

/*
 ** GiST Compress and Decompress methods for boxes
 ** do not do anything.
 */

Datum
g_cbor_compress(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(PG_GETARG_DATUM(0));
}

Datum
g_cbor_decompress(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	NDBOX	   *key = DatumGetNDBOX(PG_DETOAST_DATUM(entry->key));

	if (key != DatumGetNDBOX(entry->key))
	{
		GISTENTRY  *retval = (GISTENTRY *) palloc(sizeof(GISTENTRY));

		gistentryinit(*retval, PointerGetDatum(key), entry->rel, entry->page,
					  entry->offset, FALSE);
		PG_RETURN_POINTER(retval);
	}
	PG_RETURN_POINTER(entry);
}

/*
 ** The GiST Penalty method for boxes
 ** As in the R-tree paper, we use change in area as our penalty metric
 */
Datum
g_cbor_penalty(PG_FUNCTION_ARGS)
{
	GISTENTRY  *origentry = (GISTENTRY *) PG_GETARG_POINTER(0);
	GISTENTRY  *newentry = (GISTENTRY *) PG_GETARG_POINTER(1);
	float	   *result = (float *) PG_GETARG_POINTER(2);
	NDBOX	   *ud;
	double		tmp1,
				tmp2;

	ud = cbor_union_v0(DatumGetNDBOX(origentry->key),
					   DatumGetNDBOX(newentry->key));
	rt_cbor_size(ud, &tmp1);
	rt_cbor_size(DatumGetNDBOX(origentry->key), &tmp2);
	*result = (float) (tmp1 - tmp2);

	/*
	 * fprintf(stderr, "penalty\n"); fprintf(stderr, "\t%g\n", *result);
	 */
	PG_RETURN_FLOAT8(*result);
}

/*
 ** The GiST PickSplit method for boxes
 ** We use Guttman's poly time split algorithm
 */
Datum
g_cbor_picksplit(PG_FUNCTION_ARGS)
{
	GistEntryVector *entryvec = (GistEntryVector *) PG_GETARG_POINTER(0);
	GIST_SPLITVEC *v = (GIST_SPLITVEC *) PG_GETARG_POINTER(1);
	OffsetNumber i,
				j;
	NDBOX	   *datum_alpha,
			   *datum_beta;
	NDBOX	   *datum_l,
			   *datum_r;
	NDBOX	   *union_d,
			   *union_dl,
			   *union_dr;
	NDBOX	   *inter_d;
	bool		firsttime;
	double		size_alpha,
				size_beta,
				size_union,
				size_inter;
	double		size_waste,
				waste;
	double		size_l,
				size_r;
	int			nbytes;
	OffsetNumber seed_1 = 1,
				seed_2 = 2;
	OffsetNumber *left,
			   *right;
	OffsetNumber maxoff;

	/*
	 * fprintf(stderr, "picksplit\n");
	 */
	maxoff = entryvec->n - 2;
	nbytes = (maxoff + 2) * sizeof(OffsetNumber);
	v->spl_left = (OffsetNumber *) palloc(nbytes);
	v->spl_right = (OffsetNumber *) palloc(nbytes);

	firsttime = true;
	waste = 0.0;

	for (i = FirstOffsetNumber; i < maxoff; i = OffsetNumberNext(i))
	{
		datum_alpha = DatumGetNDBOX(entryvec->vector[i].key);
		for (j = OffsetNumberNext(i); j <= maxoff; j = OffsetNumberNext(j))
		{
			datum_beta = DatumGetNDBOX(entryvec->vector[j].key);

			/* compute the wasted space by unioning these guys */
			/* size_waste = size_union - size_inter; */
			union_d = cbor_union_v0(datum_alpha, datum_beta);
			rt_cbor_size(union_d, &size_union);
			inter_d =
				DatumGetNDBOX(
							  DirectFunctionCall2(cbor_inter, entryvec->vector[i].key, entryvec->vector[j].key));
			rt_cbor_size(inter_d, &size_inter);
			size_waste = size_union - size_inter;

			/*
			 * are these a more promising split than what we've already seen?
			 */

			if (size_waste > waste || firsttime)
			{
				waste = size_waste;
				seed_1 = i;
				seed_2 = j;
				firsttime = false;
			}
		}
	}

	left = v->spl_left;
	v->spl_nleft = 0;
	right = v->spl_right;
	v->spl_nright = 0;

	datum_alpha = DatumGetNDBOX(entryvec->vector[seed_1].key);
	datum_l = cbor_union_v0(datum_alpha, datum_alpha);
	rt_cbor_size(datum_l, &size_l);
	datum_beta = DatumGetNDBOX(entryvec->vector[seed_2].key);
	datum_r = cbor_union_v0(datum_beta, datum_beta);
	rt_cbor_size(datum_r, &size_r);

	/*
	 * Now split up the regions between the two seeds.  An important property
	 * of this split algorithm is that the split vector v has the indices of
	 * items to be split in order in its left and right vectors.  We exploit
	 * this property by doing a merge in the code that actually splits the
	 * page.
	 *
	 * For efficiency, we also place the new index tuple in this loop. This is
	 * handled at the very end, when we have placed all the existing tuples
	 * and i == maxoff + 1.
	 */

	maxoff = OffsetNumberNext(maxoff);
	for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
	{
		/*
		 * If we've already decided where to place this item, just put it on
		 * the right list.  Otherwise, we need to figure out which page needs
		 * the least enlargement in order to store the item.
		 */

		if (i == seed_1)
		{
			*left++ = i;
			v->spl_nleft++;
			continue;
		}
		else if (i == seed_2)
		{
			*right++ = i;
			v->spl_nright++;
			continue;
		}

		/* okay, which page needs least enlargement? */
		datum_alpha = DatumGetNDBOX(entryvec->vector[i].key);
		union_dl = cbor_union_v0(datum_l, datum_alpha);
		union_dr = cbor_union_v0(datum_r, datum_alpha);
		rt_cbor_size(union_dl, &size_alpha);
		rt_cbor_size(union_dr, &size_beta);

		/* pick which page to add it to */
		if (size_alpha - size_l < size_beta - size_r)
		{
			datum_l = union_dl;
			size_l = size_alpha;
			*left++ = i;
			v->spl_nleft++;
		}
		else
		{
			datum_r = union_dr;
			size_r = size_beta;
			*right++ = i;
			v->spl_nright++;
		}
	}
	*left = *right = FirstOffsetNumber; /* sentinel value, see dosplit() */

	v->spl_ldatum = PointerGetDatum(datum_l);
	v->spl_rdatum = PointerGetDatum(datum_r);

	PG_RETURN_POINTER(v);
}

/*
 ** Equality method
 */
Datum
g_cbor_same(PG_FUNCTION_ARGS)
{
	NDBOX	   *b1 = PG_GETARG_NDBOX(0);
	NDBOX	   *b2 = PG_GETARG_NDBOX(1);
	bool	   *result = (bool *) PG_GETARG_POINTER(2);

	if (cbor_cmp_v0(b1, b2) == 0)
		*result = TRUE;
	else
		*result = FALSE;

	/*
	 * fprintf(stderr, "same: %s\n", (*result ? "TRUE" : "FALSE" ));
	 */
	PG_RETURN_NDBOX(result);
}

/*
 ** SUPPORT ROUTINES
 */
bool
g_cbor_leaf_consistent(NDBOX *key, NDBOX *query, StrategyNumber strategy)
{
	bool		retval;

	/*
	 * fprintf(stderr, "leaf_consistent, %d\n", strategy);
	 */
	switch (strategy)
	{
		case RTOverlapStrategyNumber:
			retval = (bool) cbor_overlap_v0(key, query);
			break;
		case RTSameStrategyNumber:
			retval = (bool) (cbor_cmp_v0(key, query) == 0);
			break;
		case RTContainsStrategyNumber:
		case RTOldContainsStrategyNumber:
			retval = (bool) cbor_contains_v0(key, query);
			break;
		case RTContainedByStrategyNumber:
		case RTOldContainedByStrategyNumber:
			retval = (bool) cbor_contains_v0(query, key);
			break;
		default:
			retval = FALSE;
	}
	return (retval);
}

bool
g_cbor_internal_consistent(NDBOX *key, NDBOX *query,
						   StrategyNumber strategy)
{
	bool		retval;

	/*
	 * fprintf(stderr, "internal_consistent, %d\n", strategy);
	 */
	switch (strategy)
	{
		case RTOverlapStrategyNumber:
			retval = (bool) cbor_overlap_v0(key, query);
			break;
		case RTSameStrategyNumber:
		case RTContainsStrategyNumber:
		case RTOldContainsStrategyNumber:
			retval = (bool) cbor_contains_v0(key, query);
			break;
		case RTContainedByStrategyNumber:
		case RTOldContainedByStrategyNumber:
			retval = (bool) cbor_overlap_v0(key, query);
			break;
		default:
			retval = FALSE;
	}
	return (retval);
}

NDBOX *
g_cbor_binary_union(NDBOX *r1, NDBOX *r2, int *sizep)
{
	NDBOX	   *retval;

	retval = cbor_union_v0(r1, r2);
	*sizep = VARSIZE(retval);

	return (retval);
}

/* cbor_union_v0 */
NDBOX *
cbor_union_v0(NDBOX *a, NDBOX *b)
{
	int			i;
	NDBOX	   *result;
	int			dim;
	int			size;

	/* trivial case */
	if (a == b)
		return a;

	/* swap the arguments if needed, so that 'a' is always larger than 'b' */
	if (DIM(a) < DIM(b))
	{
		NDBOX	   *tmp = b;

		b = a;
		a = tmp;
	}
	dim = DIM(a);

	size = CUBE_SIZE(dim);
	result = palloc0(size);
	SET_VARSIZE(result, size);
	SET_DIM(result, dim);

	/* First compute the union of the dimensions present in both args */
	for (i = 0; i < DIM(b); i++)
	{
		result->x[i] = Min(Min(LL_COORD(a, i), UR_COORD(a, i)),
						   Min(LL_COORD(b, i), UR_COORD(b, i)));
		result->x[i + DIM(a)] = Max(Max(LL_COORD(a, i), UR_COORD(a, i)),
									Max(LL_COORD(b, i), UR_COORD(b, i)));
	}
	/* continue on the higher dimensions only present in 'a' */
	for (; i < DIM(a); i++)
	{
		result->x[i] = Min(0, Min(LL_COORD(a, i), UR_COORD(a, i)));
		result->x[i + dim] = Max(0, Max(LL_COORD(a, i), UR_COORD(a, i)));
	}

	/*
	 * Check if the result was in fact a point, and set the flag in the datum
	 * accordingly. (we don't bother to repalloc it smaller)
	 */
	if (cbor_is_point_internal(result))
	{
		size = POINT_SIZE(dim);
		SET_VARSIZE(result, size);
		SET_POINT_BIT(result);
	}

	return (result);
}

Datum
cbor_union(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0);
	NDBOX	   *b = PG_GETARG_NDBOX(1);
	NDBOX	   *res;

	res = cbor_union_v0(a, b);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_NDBOX(res);
}

/* cbor_inter */
Datum
cbor_inter(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0);
	NDBOX	   *b = PG_GETARG_NDBOX(1);
	NDBOX	   *result;
	bool		swapped = false;
	int			i;
	int			dim;
	int			size;

	/* swap the arguments if needed, so that 'a' is always larger than 'b' */
	if (DIM(a) < DIM(b))
	{
		NDBOX	   *tmp = b;

		b = a;
		a = tmp;
		swapped = true;
	}
	dim = DIM(a);

	size = CUBE_SIZE(dim);
	result = (NDBOX *) palloc0(size);
	SET_VARSIZE(result, size);
	SET_DIM(result, dim);

	/* First compute intersection of the dimensions present in both args */
	for (i = 0; i < DIM(b); i++)
	{
		result->x[i] = Max(Min(LL_COORD(a, i), UR_COORD(a, i)),
						   Min(LL_COORD(b, i), UR_COORD(b, i)));
		result->x[i + DIM(a)] = Min(Max(LL_COORD(a, i), UR_COORD(a, i)),
									Max(LL_COORD(b, i), UR_COORD(b, i)));
	}
	/* continue on the higher dimemsions only present in 'a' */
	for (; i < DIM(a); i++)
	{
		result->x[i] = Max(0, Min(LL_COORD(a, i), UR_COORD(a, i)));
		result->x[i + DIM(a)] = Min(0, Max(LL_COORD(a, i), UR_COORD(a, i)));
	}

	/*
	 * Check if the result was in fact a point, and set the flag in the datum
	 * accordingly. (we don't bother to repalloc it smaller)
	 */
	if (cbor_is_point_internal(result))
	{
		size = POINT_SIZE(dim);
		result = repalloc(result, size);
		SET_VARSIZE(result, size);
		SET_POINT_BIT(result);
	}

	if (swapped)
	{
		PG_FREE_IF_COPY(b, 0);
		PG_FREE_IF_COPY(a, 1);
	}
	else
	{
		PG_FREE_IF_COPY(a, 0);
		PG_FREE_IF_COPY(b, 1);
	}

	/*
	 * Is it OK to return a non-null intersection for non-overlapping boxes?
	 */
	PG_RETURN_NDBOX(result);
}

/* cbor_size */
Datum
cbor_size(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0);
	double		result;
	int			i;

	result = 1.0;
	for (i = 0; i < DIM(a); i++)
		result = result * Abs((LL_COORD(a, i) - UR_COORD(a, i)));

	PG_FREE_IF_COPY(a, 0);
	PG_RETURN_FLOAT8(result);
}

void
rt_cbor_size(NDBOX *a, double *size)
{
	int			i;

	if (a == (NDBOX *) NULL)
		*size = 0.0;
	else
	{
		*size = 1.0;
		for (i = 0; i < DIM(a); i++)
			*size = (*size) * Abs(UR_COORD(a, i) - LL_COORD(a, i));
	}
	return;
}

/* make up a metric in which one box will be 'lower' than the other
 -- this can be useful for sorting and to determine uniqueness */
int32
cbor_cmp_v0(NDBOX *a, NDBOX *b)
{
	int			i;
	int			dim;

	dim = Min(DIM(a), DIM(b));

	/* compare the common dimensions */
	for (i = 0; i < dim; i++)
	{
		if (Min(LL_COORD(a, i), UR_COORD(a, i)) >
			Min(LL_COORD(b, i), UR_COORD(b, i)))
			return 1;
		if (Min(LL_COORD(a, i), UR_COORD(a, i)) <
			Min(LL_COORD(b, i), UR_COORD(b, i)))
			return -1;
	}
	for (i = 0; i < dim; i++)
	{
		if (Max(LL_COORD(a, i), UR_COORD(a, i)) >
			Max(LL_COORD(b, i), UR_COORD(b, i)))
			return 1;
		if (Max(LL_COORD(a, i), UR_COORD(a, i)) <
			Max(LL_COORD(b, i), UR_COORD(b, i)))
			return -1;
	}

	/* compare extra dimensions to zero */
	if (DIM(a) > DIM(b))
	{
		for (i = dim; i < DIM(a); i++)
		{
			if (Min(LL_COORD(a, i), UR_COORD(a, i)) > 0)
				return 1;
			if (Min(LL_COORD(a, i), UR_COORD(a, i)) < 0)
				return -1;
		}
		for (i = dim; i < DIM(a); i++)
		{
			if (Max(LL_COORD(a, i), UR_COORD(a, i)) > 0)
				return 1;
			if (Max(LL_COORD(a, i), UR_COORD(a, i)) < 0)
				return -1;
		}

		/*
		 * if all common dimensions are equal, the cbor with more dimensions
		 * wins
		 */
		return 1;
	}
	if (DIM(a) < DIM(b))
	{
		for (i = dim; i < DIM(b); i++)
		{
			if (Min(LL_COORD(b, i), UR_COORD(b, i)) > 0)
				return -1;
			if (Min(LL_COORD(b, i), UR_COORD(b, i)) < 0)
				return 1;
		}
		for (i = dim; i < DIM(b); i++)
		{
			if (Max(LL_COORD(b, i), UR_COORD(b, i)) > 0)
				return -1;
			if (Max(LL_COORD(b, i), UR_COORD(b, i)) < 0)
				return 1;
		}

		/*
		 * if all common dimensions are equal, the cbor with more dimensions
		 * wins
		 */
		return -1;
	}

	/* They're really equal */
	return 0;
}

Datum
cbor_cmp(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0),
			   *b = PG_GETARG_NDBOX(1);
	int32		res;

	res = cbor_cmp_v0(a, b);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_INT32(res);
}

Datum
cbor_eq(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0),
			   *b = PG_GETARG_NDBOX(1);
	int32		res;

	res = cbor_cmp_v0(a, b);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res == 0);
}

Datum
cbor_ne(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0),
			   *b = PG_GETARG_NDBOX(1);
	int32		res;

	res = cbor_cmp_v0(a, b);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res != 0);
}

Datum
cbor_lt(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0),
			   *b = PG_GETARG_NDBOX(1);
	int32		res;

	res = cbor_cmp_v0(a, b);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res < 0);
}

Datum
cbor_gt(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0),
			   *b = PG_GETARG_NDBOX(1);
	int32		res;

	res = cbor_cmp_v0(a, b);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res > 0);
}

Datum
cbor_le(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0),
			   *b = PG_GETARG_NDBOX(1);
	int32		res;

	res = cbor_cmp_v0(a, b);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res <= 0);
}

Datum
cbor_ge(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0),
			   *b = PG_GETARG_NDBOX(1);
	int32		res;

	res = cbor_cmp_v0(a, b);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res >= 0);
}

/* Contains */
/* Box(A) CONTAINS Box(B) IFF pt(A) < pt(B) */
bool
cbor_contains_v0(NDBOX *a, NDBOX *b)
{
	int			i;

	if ((a == NULL) || (b == NULL))
		return (FALSE);

	if (DIM(a) < DIM(b))
	{
		/*
		 * the further comparisons will make sense if the excess dimensions of
		 * (b) were zeroes Since both UL and UR coordinates must be zero, we
		 * can check them all without worrying about which is which.
		 */
		for (i = DIM(a); i < DIM(b); i++)
		{
			if (LL_COORD(b, i) != 0)
				return (FALSE);
			if (UR_COORD(b, i) != 0)
				return (FALSE);
		}
	}

	/* Can't care less about the excess dimensions of (a), if any */
	for (i = 0; i < Min(DIM(a), DIM(b)); i++)
	{
		if (Min(LL_COORD(a, i), UR_COORD(a, i)) >
			Min(LL_COORD(b, i), UR_COORD(b, i)))
			return (FALSE);
		if (Max(LL_COORD(a, i), UR_COORD(a, i)) <
			Max(LL_COORD(b, i), UR_COORD(b, i)))
			return (FALSE);
	}

	return (TRUE);
}

Datum
cbor_contains(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0),
			   *b = PG_GETARG_NDBOX(1);
	bool		res;

	res = cbor_contains_v0(a, b);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res);
}

/* Contained */
/* Box(A) Contained by Box(B) IFF Box(B) Contains Box(A) */
Datum
cbor_contained(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0),
			   *b = PG_GETARG_NDBOX(1);
	bool		res;

	res = cbor_contains_v0(b, a);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res);
}

/* Overlap */
/* Box(A) Overlap Box(B) IFF (pt(a)LL < pt(B)UR) && (pt(b)LL < pt(a)UR) */
bool
cbor_overlap_v0(NDBOX *a, NDBOX *b)
{
	int			i;

	/*
	 * This *very bad* error was found in the source: if ( (a==NULL) ||
	 * (b=NULL) ) return(FALSE);
	 */
	if ((a == NULL) || (b == NULL))
		return (FALSE);

	/* swap the box pointers if needed */
	if (DIM(a) < DIM(b))
	{
		NDBOX	   *tmp = b;

		b = a;
		a = tmp;
	}

	/* compare within the dimensions of (b) */
	for (i = 0; i < DIM(b); i++)
	{
		if (Min(LL_COORD(a, i),
				UR_COORD(a, i)) > Max(LL_COORD(b, i), UR_COORD(b, i)))
			return (FALSE);
		if (Max(LL_COORD(a, i),
				UR_COORD(a, i)) < Min(LL_COORD(b, i), UR_COORD(b, i)))
			return (FALSE);
	}

	/* compare to zero those dimensions in (a) absent in (b) */
	for (i = DIM(b); i < DIM(a); i++)
	{
		if (Min(LL_COORD(a, i), UR_COORD(a, i)) > 0)
			return (FALSE);
		if (Max(LL_COORD(a, i), UR_COORD(a, i)) < 0)
			return (FALSE);
	}

	return (TRUE);
}

Datum
cbor_overlap(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0),
			   *b = PG_GETARG_NDBOX(1);
	bool		res;

	res = cbor_overlap_v0(a, b);

	PG_FREE_IF_COPY(a, 0);
	PG_FREE_IF_COPY(b, 1);
	PG_RETURN_BOOL(res);
}

/* Distance */
/* The distance is computed as a per axis sum of the squared distances
 between 1D projections of the boxes onto Cartesian axes. Assuming zero
 distance between overlapping projections, this metric coincides with the
 "common sense" geometric distance */
Datum
cbor_distance(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0),
			   *b = PG_GETARG_NDBOX(1);
	bool		swapped = false;
	double		d,
				distance;
	int			i;

	/* swap the box pointers if needed */
	if (DIM(a) < DIM(b))
	{
		NDBOX	   *tmp = b;

		b = a;
		a = tmp;
		swapped = true;
	}

	distance = 0.0;
	/* compute within the dimensions of (b) */
	for (i = 0; i < DIM(b); i++)
	{
		d = distance_1D(LL_COORD(a, i), UR_COORD(a, i), LL_COORD(b, i),
						UR_COORD(b, i));
		distance += d * d;
	}

	/* compute distance to zero for those dimensions in (a) absent in (b) */
	for (i = DIM(b); i < DIM(a); i++)
	{
		d = distance_1D(LL_COORD(a, i), UR_COORD(a, i), 0.0, 0.0);
		distance += d * d;
	}

	if (swapped)
	{
		PG_FREE_IF_COPY(b, 0);
		PG_FREE_IF_COPY(a, 1);
	}
	else
	{
		PG_FREE_IF_COPY(a, 0);
		PG_FREE_IF_COPY(b, 1);
	}

	PG_RETURN_FLOAT8(sqrt(distance));
}

static double
distance_1D(double a1, double a2, double b1, double b2)
{
	/* interval (a) is entirely on the left of (b) */
	if ((a1 <= b1) && (a2 <= b1) && (a1 <= b2) && (a2 <= b2))
		return (Min(b1, b2) - Max(a1, a2));

	/* interval (a) is entirely on the right of (b) */
	if ((a1 > b1) && (a2 > b1) && (a1 > b2) && (a2 > b2))
		return (Min(a1, a2) - Max(b1, b2));

	/* the rest are all sorts of intersections */
	return (0.0);
}

/* Test if a box is also a point */
Datum
cbor_is_point(PG_FUNCTION_ARGS)
{
	NDBOX	   *cbor = PG_GETARG_NDBOX(0);
	bool		result;

	result = cbor_is_point_internal(cbor);
	PG_FREE_IF_COPY(cbor, 0);
	PG_RETURN_BOOL(result);
}

static bool
cbor_is_point_internal(NDBOX *cbor)
{
	int			i;

	if (IS_POINT(cbor))
		return true;

	/*
	 * Even if the point-flag is not set, all the lower-left coordinates might
	 * match the upper-right coordinates, so that the value is in fact a
	 * point. Such values don't arise with current code - the point flag is
	 * always set if appropriate - but they might be present on-disk in
	 * clusters upgraded from pre-9.4 versions.
	 */
	for (i = 0; i < DIM(cbor); i++)
	{
		if (LL_COORD(cbor, i) != UR_COORD(cbor, i))
			return false;
	}
	return true;
}

/* Return dimensions in use in the data structure */
Datum
cbor_dim(PG_FUNCTION_ARGS)
{
	NDBOX	   *c = PG_GETARG_NDBOX(0);
	int			dim = DIM(c);

	PG_FREE_IF_COPY(c, 0);
	PG_RETURN_INT32(dim);
}

/* Return a specific normalized LL coordinate */
Datum
cbor_ll_coord(PG_FUNCTION_ARGS)
{
	NDBOX	   *c = PG_GETARG_NDBOX(0);
	int			n = PG_GETARG_INT16(1);
	double		result;

	if (DIM(c) >= n && n > 0)
		result = Min(LL_COORD(c, n - 1), UR_COORD(c, n - 1));
	else
		result = 0;

	PG_FREE_IF_COPY(c, 0);
	PG_RETURN_FLOAT8(result);
}

/* Return a specific normalized UR coordinate */
Datum
cbor_ur_coord(PG_FUNCTION_ARGS)
{
	NDBOX	   *c = PG_GETARG_NDBOX(0);
	int			n = PG_GETARG_INT16(1);
	double		result;

	if (DIM(c) >= n && n > 0)
		result = Max(LL_COORD(c, n - 1), UR_COORD(c, n - 1));
	else
		result = 0;

	PG_FREE_IF_COPY(c, 0);
	PG_RETURN_FLOAT8(result);
}

/* Increase or decrease box size by a radius in at least n dimensions. */
Datum
cbor_enlarge(PG_FUNCTION_ARGS)
{
	NDBOX	   *a = PG_GETARG_NDBOX(0);
	double		r = PG_GETARG_FLOAT8(1);
	int32		n = PG_GETARG_INT32(2);
	NDBOX	   *result;
	int			dim = 0;
	int			size;
	int			i,
				j;

	if (n > CUBE_MAX_DIM)
		n = CUBE_MAX_DIM;
	if (r > 0 && n > 0)
		dim = n;
	if (DIM(a) > dim)
		dim = DIM(a);

	size = CUBE_SIZE(dim);
	result = (NDBOX *) palloc0(size);
	SET_VARSIZE(result, size);
	SET_DIM(result, dim);

	for (i = 0, j = dim; i < DIM(a); i++, j++)
	{
		if (LL_COORD(a, i) >= UR_COORD(a, i))
		{
			result->x[i] = UR_COORD(a, i) - r;
			result->x[j] = LL_COORD(a, i) + r;
		}
		else
		{
			result->x[i] = LL_COORD(a, i) - r;
			result->x[j] = UR_COORD(a, i) + r;
		}
		if (result->x[i] > result->x[j])
		{
			result->x[i] = (result->x[i] + result->x[j]) / 2;
			result->x[j] = result->x[i];
		}
	}
	/* dim > a->dim only if r > 0 */
	for (; i < dim; i++, j++)
	{
		result->x[i] = -r;
		result->x[j] = r;
	}

	/*
	 * Check if the result was in fact a point, and set the flag in the datum
	 * accordingly. (we don't bother to repalloc it smaller)
	 */
	if (cbor_is_point_internal(result))
	{
		size = POINT_SIZE(dim);
		SET_VARSIZE(result, size);
		SET_POINT_BIT(result);
	}

	PG_FREE_IF_COPY(a, 0);
	PG_RETURN_NDBOX(result);
}

/* Create a one dimensional box with identical upper and lower coordinates */
Datum
cbor_f8(PG_FUNCTION_ARGS)
{
	double		x = PG_GETARG_FLOAT8(0);
	NDBOX	   *result;
	int			size;

	size = POINT_SIZE(1);
	result = (NDBOX *) palloc0(size);
	SET_VARSIZE(result, size);
	SET_DIM(result, 1);
	SET_POINT_BIT(result);
	result->x[0] = x;

	PG_RETURN_NDBOX(result);
}

/* Create a one dimensional box */
Datum
cbor_f8_f8(PG_FUNCTION_ARGS)
{
	double		x0 = PG_GETARG_FLOAT8(0);
	double		x1 = PG_GETARG_FLOAT8(1);
	NDBOX	   *result;
	int			size;

	if (x0 == x1)
	{
		size = POINT_SIZE(1);
		result = (NDBOX *) palloc0(size);
		SET_VARSIZE(result, size);
		SET_DIM(result, 1);
		SET_POINT_BIT(result);
		result->x[0] = x0;
	}
	else
	{
		size = CUBE_SIZE(1);
		result = (NDBOX *) palloc0(size);
		SET_VARSIZE(result, size);
		SET_DIM(result, 1);
		result->x[0] = x0;
		result->x[1] = x1;
	}

	PG_RETURN_NDBOX(result);
}

/* Add a dimension to an existing cbor with the same values for the new
 coordinate */
Datum
cbor_c_f8(PG_FUNCTION_ARGS)
{
	NDBOX	   *cbor = PG_GETARG_NDBOX(0);
	double		x = PG_GETARG_FLOAT8(1);
	NDBOX	   *result;
	int			size;
	int			i;

	if (IS_POINT(cbor))
	{
		size = POINT_SIZE((DIM(cbor) + 1));
		result = (NDBOX *) palloc0(size);
		SET_VARSIZE(result, size);
		SET_DIM(result, DIM(cbor) + 1);
		SET_POINT_BIT(result);
		for (i = 0; i < DIM(cbor); i++)
			result->x[i] = cbor->x[i];
		result->x[DIM(result) - 1] = x;
	}
	else
	{
		size = CUBE_SIZE((DIM(cbor) + 1));
		result = (NDBOX *) palloc0(size);
		SET_VARSIZE(result, size);
		SET_DIM(result, DIM(cbor) + 1);
		for (i = 0; i < DIM(cbor); i++)
		{
			result->x[i] = cbor->x[i];
			result->x[DIM(result) + i] = cbor->x[DIM(cbor) + i];
		}
		result->x[DIM(result) - 1] = x;
		result->x[2 * DIM(result) - 1] = x;
	}

	PG_FREE_IF_COPY(cbor, 0);
	PG_RETURN_NDBOX(result);
}

/* Add a dimension to an existing cbor */
Datum
cbor_c_f8_f8(PG_FUNCTION_ARGS)
{
	NDBOX	   *cbor = PG_GETARG_NDBOX(0);
	double		x1 = PG_GETARG_FLOAT8(1);
	double		x2 = PG_GETARG_FLOAT8(2);
	NDBOX	   *result;
	int			size;
	int			i;

	if (IS_POINT(cbor) && (x1 == x2))
	{
		size = POINT_SIZE((DIM(cbor) + 1));
		result = (NDBOX *) palloc0(size);
		SET_VARSIZE(result, size);
		SET_DIM(result, DIM(cbor) + 1);
		SET_POINT_BIT(result);
		for (i = 0; i < DIM(cbor); i++)
			result->x[i] = cbor->x[i];
		result->x[DIM(result) - 1] = x1;
	}
	else
	{
		size = CUBE_SIZE((DIM(cbor) + 1));
		result = (NDBOX *) palloc0(size);
		SET_VARSIZE(result, size);
		SET_DIM(result, DIM(cbor) + 1);
		for (i = 0; i < DIM(cbor); i++)
		{
			result->x[i] = LL_COORD(cbor, i);
			result->x[DIM(result) + i] = UR_COORD(cbor, i);
		}
		result->x[DIM(result) - 1] = x1;
		result->x[2 * DIM(result) - 1] = x2;
	}

	PG_FREE_IF_COPY(cbor, 0);
	PG_RETURN_NDBOX(result);
}

#endif
