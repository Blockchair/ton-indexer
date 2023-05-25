create table if not exists ton_block (
	id bigint primary key,
	workchain int,
	shard_id bigint,
	seqno bigint,
	roothash bytea,
	filehash bytea,
	logical_time_start bigint
);

create table if not exists ton_transaction (
	account bytea,
	hash bytea,
	logical_time bigint,
	block bigint,
	blob bytea,
	imsg_src bytea,
	imsg_dst bytea, 
	imsg_grams numeric,
	constraint tx_pkey primary key (account, hash, logical_time)
);

create table if not exists ton_validator_events (
	tx_hash bytea,
	account bytea,
	pubkey bytea,
	adnl bytea,
	logical_time bigint,
	stake numeric, 
	constraint val_e_pkey primary key (account, logical_time)
);
