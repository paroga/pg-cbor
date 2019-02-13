SET client_min_messages TO warning;
SET log_min_messages    TO warning;

-- Create the user-defined type for CBOR

CREATE FUNCTION cbor_in(cstring)
RETURNS cbor
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION cbor_out(cbor)
RETURNS cstring
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION cbor_encode(cbor)
RETURNS bytea
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION cbor_decode(bytea)
RETURNS cbor
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION cbor_recv(internal)
RETURNS cbor
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE cbor (
	INTERNALLENGTH = variable,
	INPUT = cbor_in,
	OUTPUT = cbor_out,
	RECEIVE = cbor_recv,
	SEND = cbor_encode,
	ALIGNMENT = double
);

COMMENT ON TYPE cbor IS 'Concise Binary Object Representation';


--
-- External C-functions for R-tree methods
--

-- Comparison methods

CREATE FUNCTION cbor_eq(cbor, cbor)
RETURNS bool
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

COMMENT ON FUNCTION cbor_eq(cbor, cbor) IS 'same as';

CREATE FUNCTION cbor_ne(cbor, cbor)
RETURNS bool
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

COMMENT ON FUNCTION cbor_ne(cbor, cbor) IS 'different';

CREATE FUNCTION cbor_lt(cbor, cbor)
RETURNS bool
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

COMMENT ON FUNCTION cbor_lt(cbor, cbor) IS 'lower than';

CREATE FUNCTION cbor_gt(cbor, cbor)
RETURNS bool
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

COMMENT ON FUNCTION cbor_gt(cbor, cbor) IS 'greater than';

CREATE FUNCTION cbor_le(cbor, cbor)
RETURNS bool
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

COMMENT ON FUNCTION cbor_le(cbor, cbor) IS 'lower than or equal to';

CREATE FUNCTION cbor_ge(cbor, cbor)
RETURNS bool
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

COMMENT ON FUNCTION cbor_ge(cbor, cbor) IS 'greater than or equal to';

CREATE FUNCTION cbor_cmp(cbor, cbor)
RETURNS int4
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

COMMENT ON FUNCTION cbor_cmp(cbor, cbor) IS 'btree comparison function';

CREATE FUNCTION cbor_contains(cbor, cbor)
RETURNS bool
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

COMMENT ON FUNCTION cbor_contains(cbor, cbor) IS 'contains';

CREATE FUNCTION cbor_contained(cbor, cbor)
RETURNS bool
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

COMMENT ON FUNCTION cbor_contained(cbor, cbor) IS 'contained in';

--
-- OPERATORS
--

CREATE OPERATOR < (
	LEFTARG = cbor, RIGHTARG = cbor, PROCEDURE = cbor_lt,
	COMMUTATOR = '>', NEGATOR = '>=',
	RESTRICT = scalarltsel, JOIN = scalarltjoinsel
);

CREATE OPERATOR > (
	LEFTARG = cbor, RIGHTARG = cbor, PROCEDURE = cbor_gt,
	COMMUTATOR = '<', NEGATOR = '<=',
	RESTRICT = scalargtsel, JOIN = scalargtjoinsel
);

CREATE OPERATOR <= (
	LEFTARG = cbor, RIGHTARG = cbor, PROCEDURE = cbor_le,
	COMMUTATOR = '>=', NEGATOR = '>',
	RESTRICT = scalarltsel, JOIN = scalarltjoinsel
);

CREATE OPERATOR >= (
	LEFTARG = cbor, RIGHTARG = cbor, PROCEDURE = cbor_ge,
	COMMUTATOR = '<=', NEGATOR = '<',
	RESTRICT = scalargtsel, JOIN = scalargtjoinsel
);

CREATE OPERATOR = (
	LEFTARG = cbor, RIGHTARG = cbor, PROCEDURE = cbor_eq,
	COMMUTATOR = '=', NEGATOR = '<>',
	RESTRICT = eqsel, JOIN = eqjoinsel,
	MERGES, HASHES
);

CREATE OPERATOR <> (
	LEFTARG = cbor, RIGHTARG = cbor, PROCEDURE = cbor_ne,
	COMMUTATOR = '<>', NEGATOR = '=',
	RESTRICT = neqsel, JOIN = neqjoinsel
);


-- Create the operator classes for indexing

CREATE OPERATOR CLASS cbor_ops
    DEFAULT FOR TYPE cbor USING btree AS
        OPERATOR        1       < ,
        OPERATOR        2       <= ,
        OPERATOR        3       = ,
        OPERATOR        4       >= ,
        OPERATOR        5       > ,
        FUNCTION        1       cbor_cmp(cbor, cbor);


-- hash support

CREATE FUNCTION cbor_hash(cbor)
RETURNS int4
AS 'cbor'
LANGUAGE C IMMUTABLE STRICT;

COMMENT ON FUNCTION cbor_hash(cbor) IS 'cbor hash function';

CREATE OPERATOR CLASS hash_cbor_ops
    DEFAULT FOR TYPE cbor USING hash AS
        OPERATOR	1	= ,
        FUNCTION	1	cbor_hash(cbor);
