#include "cbor.h"
#include <inttypes.h>

#include "libpq/pqformat.h"
#include "utils/builtins.h"


PG_MODULE_MAGIC;

extern int	cbor_yyparse(Cbor ** result);
extern void cbor_yyerror(Cbor ** result, const char *message);
extern void cbor_scanner_init(const char *str);
extern void cbor_scanner_finish(void);


static double		cbor_decode_half(uint64 value);
static Datum cbor_decoder(StringInfo inbuf);
static void		cbor_send_type_and_uint64_value(StringInfo buf, uint8 first_byte, uint64 value);
static void cbor_send_helper(StringInfo buf, CborEntry * cbor, int32 nr, int32 cnt);
static uint64 cbor_recv_helper_value(StringInfo inbuf, unsigned int first_byte);
static void		cbor_recv_helper(StringInfo inbuf, StringInfo outbuf, int offset, CborEntry * entry);
static void cbor_out_helper(StringInfo buf, CborEntry * cbor, int32 nr, int32 cnt);


typedef enum
{
	CborAdditionalBytes1 = 24,
	CborAdditionalBytes2 = 25,
	CborAdditionalBytes4 = 26,
	CborAdditionalBytes8 = 27
} CborAdditionalBytes;


PG_FUNCTION_INFO_V1(cbor_in);
Datum
cbor_in(PG_FUNCTION_ARGS)
{
	char	   *str = PG_GETARG_CSTRING(0);
	Cbor	   *result;

	cbor_scanner_init(str);

	if (cbor_yyparse(&result) != 0)
		cbor_yyerror(&result, "bogus input");

	cbor_scanner_finish();

	PG_RETURN_CBOR(result);
}

double
cbor_decode_half(uint64 value)
{
	uint64		sign = value & 0x8000;
	uint64		exponent = value & 0x7c00;
	uint64		fraction = value & 0x03ff;

	if (exponent == 0x7c00)
		exponent = 0x7ff << 10;
	else if (exponent)
		exponent += (1023 - 15) << 10;
	else if (fraction)
		return ldexpf(fraction, -24);

	value = sign << 48 | exponent << 42 | fraction << 42;
	return *((double *) &value);
}

uint64 cbor_recv_helper_value(StringInfo inbuf, unsigned int first_byte)
{
	if (first_byte < CborAdditionalBytes1)
		return first_byte;
	if (first_byte == CborAdditionalBytes1)
		return pq_getmsgint(inbuf, 1);
	if (first_byte == CborAdditionalBytes2)
		return pq_getmsgint(inbuf, 2);
	if (first_byte == CborAdditionalBytes4)
		return pq_getmsgint(inbuf, 4);
	if (first_byte == CborAdditionalBytes8)
		return (uint64) pq_getmsgint64(inbuf);
	if (first_byte != CBORENTRY_INDEFINITE)
		ereport(ERROR,
				(errcode(0),
				errmsg("invalid length type (%d)",
						first_byte)));
	return 0;
}

void
cbor_recv_helper(StringInfo inbuf, StringInfo outbuf, int offset, CborEntry * entry)
{
	unsigned int first_byte;
	int32		i;
	CborEntry	type;
	uint64		value;
	int			requiredSize = 0;
	void	   *data;

	first_byte = pq_getmsgint(inbuf, 1);

	type = (first_byte << 24) & CBORENTRY_TYPEMASK;
	first_byte &= 0x1f;
	value = cbor_recv_helper_value(inbuf, first_byte);

	if (first_byte == CBORENTRY_INDEFINITE && (type == CBORENTRY_TYPE_UNSIGNEDINTEGER || type == CBORENTRY_TYPE_NEGATIVEINTEGER || type == CBORENTRY_TYPE_TAG || type == CBORENTRY_TYPE_FLOATORSIMPLE))
		ereport(ERROR,
				(errcode(0),
				errmsg("type %d does not support indefinite values",
						type >> 29)));

	switch (type)
	{
		case CBORENTRY_TYPE_UNSIGNEDINTEGER:
		case CBORENTRY_TYPE_NEGATIVEINTEGER:
			requiredSize = sizeof(uint64);
			break;
		case CBORENTRY_TYPE_BYTESTRING:
		case CBORENTRY_TYPE_TEXTSTRING:
			requiredSize = INTALIGN(VARHDRSZ + value);
			break;
		case CBORENTRY_TYPE_ARRAY:
			requiredSize = sizeof(int32) + value * sizeof(CborEntry);
			break;
		case CBORENTRY_TYPE_MAP:
			requiredSize = sizeof(int32) + value * sizeof(CborEntry) * 2;
			break;
		case CBORENTRY_TYPE_TAG:
			requiredSize = sizeof(uint64) + sizeof(CborEntry);
			break;
		case CBORENTRY_TYPE_FLOATORSIMPLE:
			requiredSize = sizeof(double);
			break;
	}

	enlargeStringInfo(outbuf, requiredSize);
	data = outbuf->data + outbuf->len;
	outbuf->len += requiredSize;

	switch (type)
	{
		case CBORENTRY_TYPE_UNSIGNEDINTEGER:
		case CBORENTRY_TYPE_NEGATIVEINTEGER:
			*((uint64 *) data) = value;
			break;

		case CBORENTRY_TYPE_BYTESTRING:
		case CBORENTRY_TYPE_TEXTSTRING:
			{
				char	   *target = VARDATA(data);

				if (first_byte == CBORENTRY_INDEFINITE)
				{
					unsigned int first_byte;

					while ((first_byte = pq_getmsgint(inbuf, 1)) != CBORENTRY_BREAK)
					{
						uint64 len;

						if (((first_byte << 24) & CBORENTRY_TYPEMASK) != type)
							ereport(ERROR,
									(errcode(0),
									errmsg("invalid type %d in indefinite value of type %d",
											first_byte >> 5, type >> 29)));
						first_byte &= 0x1F;
						if (first_byte == CBORENTRY_INDEFINITE)
							ereport(ERROR,
									(errcode(0),
									errmsg("indefinite value in indefinite value of type %d",
											type >> 29)));

						len = cbor_recv_helper_value(inbuf, first_byte);
						enlargeStringInfo(outbuf, INTALIGN(value + len));
						pq_copymsgbytes(inbuf, outbuf->data + outbuf->len + value, len);
						value += len;
					}

					data = outbuf->data + outbuf->len - requiredSize;
					target = VARDATA(data);
					outbuf->len += INTALIGN(value);
				}
				else
					pq_copymsgbytes(inbuf, target, value);

				SET_VARSIZE(data, value + VARHDRSZ);
				for (i = 0; i < INTALIGN(VARHDRSZ + value) - VARHDRSZ - value; ++i)
					target[value + i] = 0;
				break;
			}

		case CBORENTRY_TYPE_ARRAY:
		case CBORENTRY_TYPE_MAP:
			{
				CborContainer *container = data;
				int			len = outbuf->len;
				bool is_map = type == CBORENTRY_TYPE_MAP;

				if (first_byte == CBORENTRY_INDEFINITE)
				{
					StringInfoData tempbuf;
					unsigned int first_byte;

					initStringInfo(&tempbuf);

					while ((first_byte = pq_getmsgint(inbuf, 1)) != CBORENTRY_BREAK)
					{
						inbuf->cursor -= 1;
						enlargeStringInfo(outbuf, sizeof(CborEntry));
						cbor_recv_helper(inbuf, &tempbuf, 0, (CborEntry*)(outbuf->data + outbuf->len));
						outbuf->len += sizeof(CborEntry);
						value += 1;
					}

					container = (CborContainer*)(outbuf->data + len - requiredSize);
					memmove(outbuf->data + outbuf->len, tempbuf.data, tempbuf.len);
					outbuf->len += tempbuf.len;
					pfree(tempbuf.data);

					if (is_map)
						value /= 2;
				}
				else
				{
					for (i = 0; i < value * (is_map ? 2 : 1); ++i)
						cbor_recv_helper(inbuf, outbuf, len, container->entries + i);
				}
				container->count = value;
				break;
			}

		case CBORENTRY_TYPE_TAG:
			{
				CborTag    *tag = data;

				tag->value = value;
				cbor_recv_helper(inbuf, outbuf, outbuf->len, &tag->entry);
				break;
			}

		case CBORENTRY_TYPE_FLOATORSIMPLE:
			{
				if (first_byte < CborAdditionalBytes2)
					*((uint64 *) data) = CBOR_SIMPLE_VALUE | value;
				else
				{
					double		dbl;

					if (first_byte == CborAdditionalBytes2)
						dbl = cbor_decode_half(value);
					else if (first_byte == CborAdditionalBytes4)
					{
						uint32		val = value;

						dbl = *((float *) &val);
					}
					else
						dbl = *((double *) &value);

					if (isnan(dbl))
						dbl = NAN;

					*((double *) data) = dbl;
				}

				break;
			}
	}

	*entry = type | (outbuf->len - offset);
}

Datum
cbor_decoder(StringInfo inbuf)
{
	StringInfoData outbuf;
	CborEntry  *entry;

	initStringInfo(&outbuf);
	enlargeStringInfo(&outbuf, VARHDRSZ + sizeof(CborEntry) + inbuf->len);
	outbuf.len = VARHDRSZ + sizeof(CborEntry);
	entry = (CborEntry *) (outbuf.data + VARHDRSZ);

	cbor_recv_helper(inbuf, &outbuf, outbuf.len, entry);

	SET_VARSIZE(outbuf.data, outbuf.len);
	PG_RETURN_CBOR(outbuf.data);
}

PG_FUNCTION_INFO_V1(cbor_recv);
Datum
cbor_recv(PG_FUNCTION_ARGS)
{
	return cbor_decoder((StringInfo) PG_GETARG_POINTER(0));
}

PG_FUNCTION_INFO_V1(cbor_decode);
Datum
cbor_decode(PG_FUNCTION_ARGS)
{
	bytea	   *data = PG_GETARG_BYTEA_P(0);
	StringInfoData inbuf;

	inbuf.maxlen = inbuf.len = VARSIZE(data);
	inbuf.data = VARDATA(data);
	inbuf.cursor = 0;

	return cbor_decoder(&inbuf);
}

void
cbor_send_type_and_uint64_value(StringInfo buf, uint8 first_byte, uint64 value)
{
	if (value < CborAdditionalBytes1)
	{
		pq_sendbyte(buf, first_byte | (uint8) value);
	}
	else if (value <= 0xff)
	{
		pq_sendbyte(buf, first_byte | CborAdditionalBytes1);
		pq_sendint(buf, value, 1);
	}
	else if (value <= 0xffff)
	{
		pq_sendbyte(buf, first_byte | CborAdditionalBytes2);
		pq_sendint(buf, value, 2);
	}
	else if (value <= 0xffffffff)
	{
		pq_sendbyte(buf, first_byte | CborAdditionalBytes4);
		pq_sendint(buf, value, 4);
	}
	else
	{
		pq_sendbyte(buf, first_byte | CborAdditionalBytes8);
		pq_sendint64(buf, value);
	}
}


void
cbor_send_helper(StringInfo buf, CborEntry * entry, int32 nr, int32 cnt)
{
	int32		i;
	uint8		type = *(entry + nr) >> 24;

	switch (*(entry + nr) & CBORENTRY_TYPEMASK)
	{
		case CBORENTRY_TYPE_UNSIGNEDINTEGER:
		case CBORENTRY_TYPE_NEGATIVEINTEGER:
			{
				uint64	   *value = CBORENTRY_VALUE(entry, nr, cnt);

				cbor_send_type_and_uint64_value(buf, type, *value);
				break;
			}
		case CBORENTRY_TYPE_BYTESTRING:
		case CBORENTRY_TYPE_TEXTSTRING:
			{
				int32		len = CBORENTRY_STRLEN(entry, nr, cnt);

				cbor_send_type_and_uint64_value(buf, type, len);
				pq_sendbytes(buf, CBORENTRY_GETSTR(entry, nr, cnt), len);
				break;
			}
		case CBORENTRY_TYPE_ARRAY:
			{
				CborContainer *value = CBORENTRY_VALUE(entry, nr, cnt);

				cbor_send_type_and_uint64_value(buf, type, value->count);
				for (i = 0; i < value->count; ++i)
					cbor_send_helper(buf, value->entries, i, value->count);
				break;
			}
		case CBORENTRY_TYPE_MAP:
			{
				CborContainer *value = CBORENTRY_VALUE(entry, nr, cnt);
				int32		count2 = value->count * 2;

				cbor_send_type_and_uint64_value(buf, type, value->count);
				for (i = 0; i < count2; ++i)
					cbor_send_helper(buf, value->entries, i, count2);
				break;
			}
		case CBORENTRY_TYPE_TAG:
			{
				CborTag    *value = CBORENTRY_VALUE(entry, nr, cnt);

				cbor_send_type_and_uint64_value(buf, type, value->value);
				cbor_send_helper(buf, &value->entry, 0, 1);
				break;
			}
		case CBORENTRY_TYPE_FLOATORSIMPLE:
			{
				uint64	   *value = CBORENTRY_VALUE(entry, nr, cnt);

				if ((*value & CBOR_SIMPLEMASK) == CBOR_SIMPLE_VALUE)
				{
					cbor_send_type_and_uint64_value(buf, type, *value & 0xFF);
				}
				else
				{
#define DBL_BASE_EXPONENT 1023
#define DBL_EXPONENT(exp) (((uint64)(DBL_BASE_EXPONENT + (exp))) << 52)

					uint64		sign = *value & 0x8000000000000000;
					uint64		exponent = *value & 0x7FF0000000000000;
					uint64		fraction = *value & 0x000FFFFFFFFFFFFF;
					bool		is_max_exponent = exponent == DBL_EXPONENT(1024);

					if (!exponent && !fraction)
					{
							pq_sendbyte(buf, type | CborAdditionalBytes2);
							pq_sendint(buf, sign >> 48, 2);
					}
					else if (fraction & 0x000000001FFFFFFF || (!is_max_exponent && (exponent < DBL_EXPONENT(-126) || DBL_EXPONENT(127) < exponent)))
					{
						pq_sendbyte(buf, type | CborAdditionalBytes8);
						pq_sendint64(buf, *value);
					}
					else if (fraction & 0x000003FFFFFFFFFF || (!is_max_exponent && (exponent < DBL_EXPONENT(-14) || DBL_EXPONENT(15) < exponent)))
					{
						uint32		single;

						if (is_max_exponent)
							exponent = 0xFF << 23;
						else
							exponent = (exponent - DBL_EXPONENT(-127)) >> 29;
						single = (sign >> 32) | exponent | (fraction >> 29);
						pq_sendbyte(buf, type | CborAdditionalBytes4);
						pq_sendint(buf, single, 4);
					}
					else
					{
						uint32		half;

						if (is_max_exponent)
							exponent = 0x1F << 10;
						else
							exponent = (exponent - DBL_EXPONENT(-15)) >> 42;
						half = (sign >> 48) | exponent | (fraction >> 42);
						pq_sendbyte(buf, type | CborAdditionalBytes2);
						pq_sendint(buf, half, 2);
					}
				}
				break;
			}
	}
}

PG_FUNCTION_INFO_V1(cbor_encode);
Datum
cbor_encode(PG_FUNCTION_ARGS)
{
	Cbor	   *cbor = PG_GETARG_CBOR(0);
	StringInfoData buf;

	pq_begintypsend(&buf);

	cbor_send_helper(&buf, &cbor->root, 0, 1);

	PG_FREE_IF_COPY(cbor, 0);
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

void
cbor_out_helper(StringInfo buf, CborEntry * entry, int32 nr, int32 cnt)
{
	int32		i;

	switch (*(entry + nr) & CBORENTRY_TYPEMASK)
	{
		case CBORENTRY_TYPE_UNSIGNEDINTEGER:
			{
				uint64_t	   *value = CBORENTRY_VALUE(entry, nr, cnt);

				appendStringInfo(buf, "%" PRIu64, *value);
				break;
			}
		case CBORENTRY_TYPE_NEGATIVEINTEGER:
			{
				uint64_t	   *value = CBORENTRY_VALUE(entry, nr, cnt);

				if (~(*value ^ 0))
					appendStringInfo(buf, "-%" PRIu64, *value + 1);
				else
					appendStringInfo(buf, "-18446744073709551616");
				break;
			}
		case CBORENTRY_TYPE_BYTESTRING:
			{
				appendStringInfoChar(buf, 'h');
				appendStringInfoChar(buf, '\'');
				for (i = 0; i < CBORENTRY_STRLEN(entry, nr, cnt); ++i)
					appendStringInfo(buf, "%02x", ((uint8 *) CBORENTRY_GETSTR(entry, nr, cnt))[i]);
				appendStringInfoChar(buf, '\'');
				break;
			}
		case CBORENTRY_TYPE_TEXTSTRING:
			{
				char	   *ch = CBORENTRY_GETSTR(entry, nr, cnt);
				int32		len = CBORENTRY_STRLEN(entry, nr, cnt);

				enlargeStringInfo(buf, len + 2);
				appendStringInfoChar(buf, '"');
				for (i = 0; i < len; ++i)
				{
					switch (ch[i])
					{
						case '\b':
							appendStringInfoString(buf, "\\b");
							break;
						case '\f':
							appendStringInfoString(buf, "\\f");
							break;
						case '\n':
							appendStringInfoString(buf, "\\n");
							break;
						case '\r':
							appendStringInfoString(buf, "\\r");
							break;
						case '\t':
							appendStringInfoString(buf, "\\t");
							break;
						case '\\':
							appendStringInfoString(buf, "\\\\");
							break;
						case '\"':
							appendStringInfoString(buf, "\\\"");
							break;
						default:
							appendStringInfoChar(buf, ch[i]);
					}
				}

				appendStringInfoChar(buf, '"');
				break;
			}
		case CBORENTRY_TYPE_ARRAY:
			{
				CborContainer *value = CBORENTRY_VALUE(entry, nr, cnt);

				appendStringInfoChar(buf, '[');
				for (i = 0; i < value->count; ++i)
				{
					if (i)
						appendStringInfoString(buf, ", ");
					cbor_out_helper(buf, value->entries, i, value->count);
				}
				appendStringInfoChar(buf, ']');
				break;
			}
		case CBORENTRY_TYPE_MAP:
			{
				CborContainer *value = CBORENTRY_VALUE(entry, nr, cnt);

				appendStringInfoChar(buf, '{');
				for (i = 0; i < value->count; ++i)
				{
					if (i)
						appendStringInfoString(buf, ", ");
					cbor_out_helper(buf, value->entries, i * 2 + 0, value->count * 2);
					appendStringInfoString(buf, ": ");
					cbor_out_helper(buf, value->entries, i * 2 + 1, value->count * 2);
				}
				appendStringInfoChar(buf, '}');
				break;
			}
		case CBORENTRY_TYPE_TAG:
			{
				CborTag    *value = CBORENTRY_VALUE(entry, nr, cnt);
				uint64_t tag = value->value;

				appendStringInfo(buf, "%" PRIu64 "(", tag);
				cbor_out_helper(buf, &value->entry, 0, 1);
				appendStringInfoChar(buf, ')');
				break;
			}
		case CBORENTRY_TYPE_FLOATORSIMPLE:
			{
				uint64	   *value = CBORENTRY_VALUE(entry, nr, cnt);

				if ((*value & CBOR_SIMPLEMASK) == CBOR_SIMPLE_VALUE)
				{
					uint8		simple = *value & 0xFF;

					switch (simple)
					{
						case 20:
							appendStringInfoString(buf, "false");
							break;
						case 21:
							appendStringInfoString(buf, "true");
							break;
						case 22:
							appendStringInfoString(buf, "null");
							break;
						case 23:
							appendStringInfoString(buf, "undefined");
							break;
						default:
							appendStringInfo(buf, "simple(%u)", simple);
					}
				}
				else
				{
					double		flt = *((double *) value);

					if (isfinite(flt))
					{
						int i = buf->len;
						appendStringInfo(buf, "%g", flt);
						while (i < buf->len)
						{
							char ch = buf->data[i];
							if (ch == '.' || ch == 'e')
								break;
							i +=1;
						}
						if (i == buf->len)
							appendStringInfoString(buf, ".0");
					}
					else if (isnan(flt))
						appendStringInfoString(buf, "NaN");
					else if (flt > 0)
						appendStringInfoString(buf, "Infinity");
					else
						appendStringInfoString(buf, "-Infinity");
				}
				break;
			}
	}
}

PG_FUNCTION_INFO_V1(cbor_out);
Datum
cbor_out(PG_FUNCTION_ARGS)
{
	Cbor	   *cbor = PG_GETARG_CBOR(0);
	StringInfoData buf;

	initStringInfo(&buf);
	cbor_out_helper(&buf, &cbor->root, 0, 1);

	PG_FREE_IF_COPY(cbor, 0);
	PG_RETURN_CSTRING(buf.data);
}
