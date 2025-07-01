/* contrib/pg_wal_recovery/pg_wal_recovery--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_wal_recovery" to load this file. \quit

--
-- wal_recover()
--

-- CREATE FUNCTION wal_recover(IN wal_dir TEXT, IN start_lsn TEXT DEFAULT '0/0'::TEXT, OUT wal_type TEXT, OUT wal_record TEXT)
CREATE FUNCTION wal_recover(IN wal_dir text, OUT wal_type text, OUT wal_record text)
RETURNS record
AS 'MODULE_PATHNAME', 'recover'
LANGUAGE C;

CREATE FUNCTION wal_list_records(IN wal_dir TEXT, IN start_lsn TEXT DEFAULT '0/0'::TEXT, OUT wal_file_name TEXT, OUT wal_type TEXT, OUT wal_record text)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'show_records'
LANGUAGE C;