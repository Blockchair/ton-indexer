## TON-indexer

Tool for indexing and storing TON blockchain activity into a PostgreSQL database for further processing. Intended to be used as a standalone module over the TON-core using lite-api from it.

## Manual Setup
1. get latest mainnet version of ton core from upstream
2. copy medium-client source code folder into root of ton project
3. modify CMake in project-root directiory:
   * either add following line to the list of subdirectories in it
   > add_subdirectory(medium-client)
   * or, used sed:
   > sed -i "s/add_subdirectory(lite-client)/add_subdirectory(lite-client)\nadd_subdirectory(medium-client)/" CMakeLists.txt
4. install libpqxx with all dependencies (note, you will need this specific version, anything newer requires C++17, which TON won't appreciate):
	> apt install libpq-dev
	>
	> wget https://github.com/jtv/libpqxx/archive/refs/tags/6.4.8.tar.gz --output-document=libpqxx.tar.gz
	>
	> tar -xvzf libpqxx.tar.gz
	>
	> cd libpqxx-6.4.8
	>
	> chmod +x configure && \
	>
	>    ./configure CXX='clang++' --disable-documentation CXXFLAGS='-std=c++14' && \
    >
	>    make && make install
5. move FindPQXX.cmake into CMake subdirectory of ton-project-root
6. configure the project as recommended in ton-core readme
7. build the medium-client target
   > cmake --build . --target medium-client
8. move the medium-client.config into same directory as compiled binarary, and modify fields as needed
9. (optional) download global-config file for mainnet, if you dont not plan running your own node to connect to, and move it into same dir as 6.
10. setup PostgreSQL and tables on the target machine. Be warned, TON can generate quite a lot of data quite quickly
   > psql -U \<postgres-user\> -w -d \<postgresql-db\> -f ./create_db.sql

## Docker setup
Please refer to [this Dockerfile](https://github.com/Blockchair/ton-indexer/blob/main/Dockerfile).

## Usage
There is a convenience shell script that enforces database/tables existence before passing all provided arguments to the indexer. You can modify it or set corresponding environment variables
> Usage: indexer.sh (-a 0.1.2.3:4 -p pubkey.pub | -g) [-n] [-H] [-C medium-client.config] [-v]

Indexer support following options:
> -a | --addr \<ip:port\>			get data from a specific ton node, requires pubkey file to be provided\
> -p | --pub \<pubkey-file-name\>	pubkey for the handshake, only used with -a\
> -g | --global						connect to a random node from ./ton-global.config\
> -n | --no-down					do not ask for blocks happened before earlies known to the indexer, can be helpful with non-archival nodes\
> -H | --historic                    do not ask for blocks happened after latest known to the indexer\
> -C | --config \<config-file-name\> config file for connection/database constants, when not provided will attempt to use ./medium-client.config\
> -v | --verbose                     set maximum verbosity, prints EVERYTHING. very slow, very cluttered, great for debugging

Config file options:
> db_write_batchsize : int - every this many masterchain blocks indexer will perform a DB insert transaction\
> adnl_max_retry : int - after this many successive failed attempts to get something for the node program will terminate\
> db_conn_string : string - configuration for PostgreSQL connection that can be understood by the driver\
> print_every : int - how frequently (in masterchain blocks) indexer is to report on its current progress

## Stopping and restarting
Should indexer be restarted, it has a built-in catch-up mechanism that allows it to start recording the newest information regardless of how long it was offline. However, said mechanism only fills the gap between `now` and `latest-found-in-database` i.e. it will NOT fill multiple gaps. With that in mind, do not stop the indexer until `middle-branch` reported to have finished, otherwise the database integrity may be compromised.

## Extending

As with any other blockchain, one can indefinitely create custom formats of message body for their smart-contracts. Making the task of creating a one-size-fits-all parsing tool effectively impossible. With that in mind, TON-indexer provides generic interface for creating custom message parsing functions, for in-depth instructions and examples, please refer to 
> tx-parsers.h

Additionally, the CLI tool for testing said functions is provided in the repo under

> tester.cpp

Add entry-points, desired tests and build transaction-tester

> cmake --build . --target transaction-tester

Finally invoke with

> ./transaction-tester full-hex-string-of-the-transaction

This is executed locally and does not require database

## License

TON Indexer is released under the terms of the MIT license. See [LICENSE.md](https://github.com/Blockchair/ton-indexer/blob/main/LICENSE.md) for more information.

By contributing to this repository, you agree to license your work under the MIT license unless specified otherwise at the top of the file itself. Any work contributed where you are not the original author must contain its license header with the original author and source.