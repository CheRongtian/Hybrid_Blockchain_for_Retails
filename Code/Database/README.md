# Local Database

The control server stores local supply-chain data in `supply_chain.db`.

SQLite database files and their WAL/SHM files are ignored by Git. The database
is created automatically when `control_server` starts.
