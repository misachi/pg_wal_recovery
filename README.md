# pg_recovery

`pg_recovery` is an educational PostgreSQL extension for database recovery, focusing on restoring databases from Write-Ahead Logs (WAL) and supporting point-in-time recovery.

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
git clone https://github.com/misachi/pg_recovery.git
cd pg_recovery
make
make install
```

Then, enable the extension in your database:

```sql
CREATE EXTENSION pg_recovery;
```

## Usage

### List WAL Records

List WAL records available for recovery from a specified directory:

```sql
SELECT * FROM wal_list_records('/tmp');
```

### Replay WAL Records

Replay WAL records to restore the database:

```sql
SELECT * FROM wal_recover('/tmp');
```

### Point-in-Time Recovery

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

This extension is intended for educational purposes. Use with caution on production systems.