\set ECHO none
BEGIN;
\i sql/cbor.sql
\set ECHO all

--
--  Test cbor datatype
--

CREATE TYPE cbor_encode_decode_test_result AS (decoded cbor, encoded bytea, parsed cbor);

CREATE FUNCTION cbor_encode_decode_test(bytea) RETURNS cbor_encode_decode_test_result
    AS $$ SELECT cbor_decode($1), cbor_encode(cbor_decode($1)), cbor_decode($1)::text::cbor$$
    LANGUAGE SQL;

--
-- official test cases
--
SELECT * FROM cbor_encode_decode_test('\x00');
SELECT * FROM cbor_encode_decode_test('\x01');
SELECT * FROM cbor_encode_decode_test('\x0a');
SELECT * FROM cbor_encode_decode_test('\x17');
SELECT * FROM cbor_encode_decode_test('\x1818');
SELECT * FROM cbor_encode_decode_test('\x1819');
SELECT * FROM cbor_encode_decode_test('\x1864');
SELECT * FROM cbor_encode_decode_test('\x1903e8');
SELECT * FROM cbor_encode_decode_test('\x1a000f4240');
SELECT * FROM cbor_encode_decode_test('\x1b000000e8d4a51000');
SELECT * FROM cbor_encode_decode_test('\x1bffffffffffffffff');
SELECT * FROM cbor_encode_decode_test('\xc249010000000000000000');
SELECT * FROM cbor_encode_decode_test('\x3bffffffffffffffff');
SELECT * FROM cbor_encode_decode_test('\xc349010000000000000000');
SELECT * FROM cbor_encode_decode_test('\x20');
SELECT * FROM cbor_encode_decode_test('\x29');
SELECT * FROM cbor_encode_decode_test('\x3863');
SELECT * FROM cbor_encode_decode_test('\x3903e7');
SELECT * FROM cbor_encode_decode_test('\xf90000');
SELECT * FROM cbor_encode_decode_test('\xf98000');
SELECT * FROM cbor_encode_decode_test('\xf93c00');
SELECT * FROM cbor_encode_decode_test('\xfb3ff199999999999a');
SELECT * FROM cbor_encode_decode_test('\xf93e00');
SELECT * FROM cbor_encode_decode_test('\xf97bff');
SELECT * FROM cbor_encode_decode_test('\xfa47c35000');
SELECT * FROM cbor_encode_decode_test('\xfa7f7fffff');
SELECT * FROM cbor_encode_decode_test('\xfb7e37e43c8800759c');
SELECT * FROM cbor_encode_decode_test('\xf90001');
SELECT * FROM cbor_encode_decode_test('\xf90400');
SELECT * FROM cbor_encode_decode_test('\xf9c400');
SELECT * FROM cbor_encode_decode_test('\xfbc010666666666666');
SELECT * FROM cbor_encode_decode_test('\xf97c00');
SELECT * FROM cbor_encode_decode_test('\xf97e00');
SELECT * FROM cbor_encode_decode_test('\xf9fc00');
SELECT * FROM cbor_encode_decode_test('\xfa7f800000');
SELECT * FROM cbor_encode_decode_test('\xfa7fc00000');
SELECT * FROM cbor_encode_decode_test('\xfaff800000');
SELECT * FROM cbor_encode_decode_test('\xfb7ff0000000000000');
SELECT * FROM cbor_encode_decode_test('\xfb7ff8000000000000');
SELECT * FROM cbor_encode_decode_test('\xfbfff0000000000000');
SELECT * FROM cbor_encode_decode_test('\xf4');
SELECT * FROM cbor_encode_decode_test('\xf5');
SELECT * FROM cbor_encode_decode_test('\xf6');
SELECT * FROM cbor_encode_decode_test('\xf7');
SELECT * FROM cbor_encode_decode_test('\xf0');
SELECT * FROM cbor_encode_decode_test('\xf818');
SELECT * FROM cbor_encode_decode_test('\xf8ff');
SELECT * FROM cbor_encode_decode_test('\xc074323031332d30332d32315432303a30343a30305a');
SELECT * FROM cbor_encode_decode_test('\xc11a514b67b0');
SELECT * FROM cbor_encode_decode_test('\xc1fb41d452d9ec200000');
SELECT * FROM cbor_encode_decode_test('\xd74401020304');
SELECT * FROM cbor_encode_decode_test('\xd818456449455446');
SELECT * FROM cbor_encode_decode_test('\xd82076687474703a2f2f7777772e6578616d706c652e636f6d');
SELECT * FROM cbor_encode_decode_test('\x40');
SELECT * FROM cbor_encode_decode_test('\x4401020304');
SELECT * FROM cbor_encode_decode_test('\x60');
SELECT * FROM cbor_encode_decode_test('\x6161');
SELECT * FROM cbor_encode_decode_test('\x6449455446');
SELECT * FROM cbor_encode_decode_test('\x62225c');
SELECT * FROM cbor_encode_decode_test('\x62c3bc');
SELECT * FROM cbor_encode_decode_test('\x63e6b0b4');
SELECT * FROM cbor_encode_decode_test('\x64f0908591');
SELECT * FROM cbor_encode_decode_test('\x80');
SELECT * FROM cbor_encode_decode_test('\x83010203');
SELECT * FROM cbor_encode_decode_test('\x8301820203820405');
SELECT * FROM cbor_encode_decode_test('\x98190102030405060708090a0b0c0d0e0f101112131415161718181819');
SELECT * FROM cbor_encode_decode_test('\xa0');
SELECT * FROM cbor_encode_decode_test('\xa201020304');
SELECT * FROM cbor_encode_decode_test('\xa26161016162820203');
SELECT * FROM cbor_encode_decode_test('\x826161a161626163');
SELECT * FROM cbor_encode_decode_test('\xa56161614161626142616361436164614461656145');
SELECT * FROM cbor_encode_decode_test('\x5f42010243030405ff');
SELECT * FROM cbor_encode_decode_test('\x7f657374726561646d696e67ff');
SELECT * FROM cbor_encode_decode_test('\x9fff');
SELECT * FROM cbor_encode_decode_test('\x9f018202039f0405ffff');
SELECT * FROM cbor_encode_decode_test('\x9f01820203820405ff');
SELECT * FROM cbor_encode_decode_test('\x83018202039f0405ff');
SELECT * FROM cbor_encode_decode_test('\x83019f0203ff820405');
SELECT * FROM cbor_encode_decode_test('\x9f0102030405060708090a0b0c0d0e0f101112131415161718181819ff');
SELECT * FROM cbor_encode_decode_test('\xbf61610161629f0203ffff');
SELECT * FROM cbor_encode_decode_test('\x826161bf61626163ff');
SELECT * FROM cbor_encode_decode_test('\xbf6346756ef563416d7421ff');

--
-- Additional encoding tests
--
SELECT * FROM cbor_encode_decode_test('\x3bfffffffffffffffe');

--
-- comparison functions tests
--

SELECT '1'::cbor = '1'::cbor;
SELECT '1'::cbor <> '1'::cbor;
SELECT '1'::cbor < '1'::cbor;
SELECT '1'::cbor <= '1'::cbor;
SELECT '1'::cbor > '1'::cbor;
SELECT '1'::cbor >= '1'::cbor;

SELECT '1'::cbor = '2'::cbor;
SELECT '1'::cbor <> '2'::cbor;
SELECT '1'::cbor < '2'::cbor;
SELECT '1'::cbor <= '2'::cbor;
SELECT '1'::cbor > '2'::cbor;
SELECT '1'::cbor >= '2'::cbor;

SELECT '2'::cbor = '1'::cbor;
SELECT '2'::cbor <> '1'::cbor;
SELECT '2'::cbor < '1'::cbor;
SELECT '2'::cbor <= '1'::cbor;
SELECT '2'::cbor > '1'::cbor;
SELECT '2'::cbor >= '1'::cbor;

SELECT '0'::cbor < '[]'::cbor;
SELECT '[]'::cbor < '{}'::cbor;

--
-- hash function tests
--

SELECT cbor_hash('1'::cbor) = cbor_hash('1'::cbor);
SELECT cbor_hash('{"a": 1, "b": [2, 3]}'::cbor) = cbor_hash('{"a": 1, "b": [2, 3]}'::cbor);

ROLLBACK;
