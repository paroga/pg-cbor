cbor 0.1.0
==========

[![PGXN version](https://badge.fury.io/pg/cbor.svg)](https://badge.fury.io/pg/cbor)
[![Build Status](https://travis-ci.org/paroga/pg-cbor.png)](https://travis-ci.org/paroga/pg-cbor)

This library contains a single PostgreSQL extension, a data type called
`cbor`. It's an implementation of the Concise Binary Object Representation
specified by [RFC 7049](https://tools.ietf.org/html/rfc7049).

Installation
------------

To build it, just do this:

    make
    make install
    make installcheck

If you encounter an error such as:

    "Makefile", line 8: Need an operator

You need to use GNU make, which may well be installed on your system as
`gmake`:

    gmake
    gmake install
    gmake installcheck

If you encounter an error such as:

    make: pg_config: Command not found

Be sure that you have `pg_config` installed and in your path. If you used a
package management system such as RPM to install PostgreSQL, be sure that the
`-devel` package is also installed. If necessary tell the build process where
to find it:

    env PG_CONFIG=/path/to/pg_config make && make installcheck && make install

If you encounter an error such as:

    ERROR:  must be owner of database regression

You need to run the test suite using a super user, such as the default
"postgres" super user:

    make installcheck PGUSER=postgres

Once cbor is installed, you can add it to a database. If you're running
PostgreSQL 9.1.0 or greater, it's a simple as connecting to a database as a
super user and running:

    CREATE EXTENSION cbor;

Dependencies
------------
The `cbor` data type has no dependencies other than PostgreSQL.

Copyright and License
---------------------

Copyright (c) 2010-2018 Patrick Gansterer.

This module is free software; you can redistribute it and/or modify it under
the [PostgreSQL License](http://www.opensource.org/licenses/postgresql).

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose, without fee, and without a written agreement is
hereby granted, provided that the above copyright notice and this paragraph
and the following two paragraphs appear in all copies.

In no event shall Patrick Gansterer be liable to any party for direct,
indirect, special, incidental, or consequential damages, including lost
profits, arising out of the use of this software and its documentation, even
if Patrick Gansterer has been advised of the possibility of such damage.

Patrick Gansterer specifically disclaims any warranties, including, but not
limited to, the implied warranties of merchantability and fitness for a
particular purpose. The software provided hereunder is on an "as is" basis,
and Patrick Gansterer has no obligations to provide maintenance, support,
updates, enhancements, or modifications.
