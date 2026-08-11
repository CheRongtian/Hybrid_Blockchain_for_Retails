# Local Database

The control server stores local supply-chain data in supply_chain.db.

The database contains batch master data, role-specific event data, Merkle
verification snapshots, IPFS CID references, and block connections. Large file
contents remain in IPFS; this directory stores the returned CID and metadata.

SQLite database files and their WAL/SHM files are ignored by Git. The database
is created automatically when control_server starts.
