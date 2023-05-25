# builder
FROM ubuntu:20.04 as ton_indexer_builder
RUN apt-get update && \
	DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake clang-6.0 clang++-6.0 postgresql libpq-dev nano \
	libtool openssl libssl-dev zlib1g-dev gperf wget tar git autoconf libpq-dev libmicrohttpd-dev pkg-config && \
	rm -rf /var/lib/apt/lists/*

ENV CC clang-6.0
ENV CXX clang++-6.0

# libpqxx
RUN wget https://github.com/jtv/libpqxx/archive/refs/tags/6.4.8.tar.gz --output-document=libpqxx.tar.gz
RUN tar -xvzf libpqxx.tar.gz

WORKDIR /libpqxx-6.4.8
RUN chmod +x configure && \
	./configure CXX='clang++' --disable-documentation CXXFLAGS='-std=c++14' && \
	make && make install

WORKDIR /

# get ton-core
RUN git clone --recursive https://github.com/ton-blockchain/ton
ADD . /ton-indexer
# get ton-indexer

# inject build routines
RUN cp -r ton-indexer/medium-client/ ton/ && \
    cp ton/medium-client/FindPQXX.cmake ton/CMake/

RUN sed -i "s/add_subdirectory(lite-client)/add_subdirectory(lite-client)\nadd_subdirectory(medium-client)/" ton/CMakeLists.txt

# build medium client
RUN mkdir build && cd build && cmake ../ton && cmake --build . --target medium-client 


FROM ubuntu:20.04 as ton_indexer

ARG DEBIAN_FRONTEND=noninteractive

RUN apt update	&& \
	apt install lsb-release ca-certificates apt-transport-https software-properties-common \
	build-essential openssl libssl-dev postgresql -y

RUN mkdir -p /var/ton-indexer && \
	mkdir -p /var/ton-indexer/static

COPY --from=ton_indexer_builder /usr/local/lib/libpqxx.a /usr/local/lib/libpqxx.a
COPY --from=ton_indexer_builder /usr/local/include/pqxx /usr/include/pqxx

COPY --from=ton_indexer_builder /build/medium-client/medium-client /var/ton-indexer/medium-client
COPY --from=ton_indexer_builder /ton/medium-client/create_db.sql /var/ton-indexer/create_db.sql
COPY --from=ton_indexer_builder /ton-indexer/medium-client.config /var/ton-indexer/medium-client.config
COPY --from=ton_indexer_builder /ton-indexer/ton-global.config /var/ton-indexer/ton-global.config


WORKDIR /var/ton-indexer

ENTRYPOINT ["./medium-client", "-g"]
