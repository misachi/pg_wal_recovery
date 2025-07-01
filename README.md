# pg_wal_recovery

`pg_wal_recovery` is an educational PostgreSQL extension for database recovery, focusing on restoring databases from Write-Ahead Logs (WAL) and supporting point-in-time recovery.

## Features

- **Restore from WAL:** Recover your PostgreSQL database using archived WAL files.
- **Point-in-Time Recovery:** Restore your database to a specific moment.
- **WAL Record Inspection:** List and inspect WAL records before replaying.
- **Simple SQL Interface:** Use SQL functions to manage recovery operations.

## Requirements

- PostgreSQL 16 or newer
- C compiler and PostgreSQL development headers

## Installation

Clone the repository and build the extension:

```bash
git clone https://github.com/misachi/pg_wal_recovery.git
cd pg_wal_recovery
make
make install
```

Then, enable the extension in your database:

```sql
CREATE EXTENSION pg_wal_recovery;
```

## Usage

### List WAL Records

List WAL records available for recovery from a specified directory:

```sql
SELECT * FROM wal_list_records('/tmp');
```

Example
```
postgres=# SELECT * FROM wal_list_records('/tmp');
WARNING:  invalid record length at 16/20598F0: expected at least 24, got 0
      wal_file_name       |     wal_type      | wal_record
--------------------------+-------------------+------------
 000000010000001600000002 | CHECKPOINT_REDO   | 16/2057B18
 000000010000001600000002 | RUNNING_XACTS     | 16/2057B38
 000000010000001600000002 | CHECKPOINT_ONLINE | 16/2057B70
 000000010000001600000002 | RUNNING_XACTS     | 16/2057BE8
 000000010000001600000002 | FPI_FOR_HINT      | 16/2057C20
 000000010000001600000002 | HOT_UPDATE        | 16/2057C90
 000000010000001600000002 | HOT_UPDATE        | 16/2057CD8
 000000010000001600000002 | HOT_UPDATE        | 16/2057D20
 ....
 000000010000001600000002 | HOT_UPDATE        | 16/20597F0
 000000010000001600000002 | HOT_UPDATE        | 16/2059838
 000000010000001600000002 | HOT_UPDATE        | 16/2059880
 000000010000001600000002 | COMMIT            | 16/20598C8
(106 rows)
```

List starting from a specific position in the WAL segment
```
postgres=# SELECT * FROM wal_list_records('/tmp', '16/2057C90');
WARNING:  invalid record length at 16/20598F0: expected at least 24, got 0
      wal_file_name       |     wal_type      | wal_record
--------------------------+-------------------+------------
 000000010000001600000002 | HOT_UPDATE        | 16/2057C90
 000000010000001600000002 | HOT_UPDATE        | 16/2057CD8
 000000010000001600000002 | HOT_UPDATE        | 16/2057D20
 ....
 000000010000001600000002 | HOT_UPDATE        | 16/20597F0
 000000010000001600000002 | HOT_UPDATE        | 16/2059838
 000000010000001600000002 | HOT_UPDATE        | 16/2059880
 000000010000001600000002 | COMMIT            | 16/20598C8
(101 rows)
```

### Replay WAL Records

Replay WAL records to restore the database:

```sql
SELECT * FROM wal_recover('/tmp');
```

It returns the last record replayed

Example
```
postgres=# select * from wal_recover('/tmp');
WARNING:  Page LSN 16/3012920 is greater than record LSN 16/2057CD8 for record at: 16/2057C90
WARNING:  invalid record length at 16/20598F0: expected at least 24, got 0
 wal_type | wal_record
----------+------------
 COMMIT   | 16/20598C8
(1 row)

```

### Point-in-Time Recovery(WIP)

To recover to a specific timestamp, use:

```sql
SELECT * FROM wal_recover('/tmp', '2024-06-01 12:00:00');
```

> **Note:** Replace `/tmp` with the path to your WAL archive directory.

## Contributing

Contributions are welcome! Please open issues for bugs or feature requests, or submit pull requests for improvements.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Disclaimer

This extension is intended for educational purposes. Don't use in production systems.