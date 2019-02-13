ALTER EXTENSION cbor ADD type cbor;
ALTER EXTENSION cbor ADD function cbor_in(cstring);
ALTER EXTENSION cbor ADD function cbor_out(cbor);
ALTER EXTENSION cbor ADD function cbor_encode(cbor);
ALTER EXTENSION cbor ADD function cbor_decode(bytea);
ALTER EXTENSION cbor ADD function cbor_recv(internal);
