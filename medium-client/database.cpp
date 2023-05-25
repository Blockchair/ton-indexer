#include "database.h"
#include <iostream>

_block_id getMCRow(pqxx::connection* conn, SelectEdge edge) {
	pqxx::work transaction(*conn);

	auto sql = sql_get_edge_mc_1_;

	if (edge == SelectEdge::LO) {
		sql += sql_asc_;
	}
	if (edge == SelectEdge::HI) {
		sql += sql_desc_;
	}
	sql += "limit 1";
	pqxx::result res = transaction.exec(sql);

	if (res[0][0].is_null()) {
		return {0, 0, 0, 0, "", ""};
	}
	auto workchain = res[0][0].as<int32_t>();
	auto shard_id = res[0][1].as<int64_t>();
	auto seqno = res[0][2].as<uint32_t>();
	auto roothash = res[0][3].as<std::string>().substr(2);
	auto filehash = res[0][4].as<std::string>().substr(2);

	return {workchain, seqno, shard_id, 0, roothash, filehash};
}

std::vector<_block_id> getEdgeRow(pqxx::connection* conn, SelectEdge edge) {
	pqxx::work transaction(*conn);
	
	auto sql = sql_get_edge_row_1_;
	
	if (edge == SelectEdge::LO) {
		sql += sql_minSeqno;
	}
	if (edge == SelectEdge::HI) {
		sql += sql_maxSeqno;
	}
	sql += sql_get_edge_row_2_;
	
	pqxx::result res = transaction.exec(sql);

	std::vector<_block_id> ret;

	if (res[0][0].is_null()) {
		return ret;
	}
	for (const auto& row : res) {	
		auto workchain = row[0].as<int32_t>();
		auto shard_id = row[1].as<int64_t>();
		auto seqno = row[2].as<uint32_t>();
		auto roothash = row[3].as<std::string>().substr(2);
		auto filehash = row[4].as<std::string>().substr(2);

		ret.push_back({workchain, seqno, shard_id, 0, roothash, filehash});
	}
	return ret;
}

_block_id getBlkBy3Keys(pqxx::connection* conn, int32_t workchain, int64_t shard_id, uint32_t seqno) {
	pqxx::work transaction(*conn);
	pqxx::result res = transaction.exec(
		"select roothash, filehash from ton_block where "
		"workchain = " + std::to_string(workchain) + 
		" and shard_id = " + std::to_string(shard_id) +
		" and seqno = " + std::to_string(seqno)
	);
	transaction.commit();

	if (res[0][0].is_null()) {
		return {workchain, seqno, shard_id, 0, "", ""};
	}

	auto roothash = res[0][0].as<std::string>().substr(2);
	auto filehash = res[0][1].as<std::string>().substr(2);
	
	return {workchain, seqno, shard_id, 0, roothash, filehash};
}

uint64_t getNextCounter(pqxx::connection* conn) {
	pqxx::work transaction(*conn);
	pqxx::result res = transaction.exec("select max(id)+1 from ton_block");
	transaction.commit();
	return (!res[0][0].is_null()) ? res[0][0].as<uint64_t>() : 0;
}


// as cool as stream-insert is, it does not have on-conflict-do-smth clause; it WILL rollback EVERYTHING -- ie counter is my responsibility, no crutches
uint64_t insertBlocks(pqxx::connection* conn, std::vector<_block_primitive>& blocks, uint64_t counter) {
	pqxx::work transaction(*conn);
	
	std::string table_name = "ton_block";
	std::vector<std::string> table_cols = std::vector<std::string>{"id", "workchain", "shard_id", "seqno", "roothash", "filehash", "logical_time_start"};
	
	try {
		pqxx::stream_to stream{transaction, table_name, table_cols};
		for (auto& blk : blocks) {
			blk.blk_id.id = counter;
			
			_block_row blk_row{counter, blk.blk_id.workchain, blk.blk_id.shard_id, blk.blk_id.seqno, blk.blk_id.roothash, blk.blk_id.filehash, 0};

			stream << blk_row;
				
			counter++;
		}
		stream.complete();
	}
	catch (const pqxx::unique_violation& ex) {
		std::cout<<"db write error: block duplication attempted\n";
		transaction.abort();
		throw std::runtime_error("unique key violation (block)");
	}
	catch (const std::exception& ex) {
		transaction.abort();
		__throw_exception_again;
	}

	table_name = "ton_transaction";
	table_cols = std::vector<std::string>{"account", "hash", "logical_time", "block", "blob", "imsg_src", "imsg_dst", "imsg_grams"};	
	
	try {
		pqxx::stream_to stream2{transaction, table_name, table_cols};
		for (auto& blk : blocks) {
			for (auto& tx : blk.transactions) {
				_transaction_row tx_row{tx.account, tx.hash, tx.lt, blk.blk_id.id, tx.blob, tx.imsg_src, tx.imsg_dst, tx.grams};

				stream2 << tx_row;
			}
		}
		stream2.complete();
	}
	catch (const pqxx::unique_violation& ex) {
		std::cout<<"db write error: transaction duplication attempted\n";
		transaction.abort();
		throw std::runtime_error("unique key violation (transaction)");
	}
	catch (const std::exception& ex) {
		transaction.abort();
		__throw_exception_again;
	}

	transaction.commit();

	return counter;
}


void test(pqxx::connection* conn) {
	pqxx::work transaction(*conn);

	pqxx::result res = transaction.exec("SELECT 1, 2");
	transaction.commit();

	for (const auto& row : res) {
		for (const auto& elt : row) {
			std::cout<<elt.as<int>()<<'\t';
		}
		std::cout<<'\n';
	}
}

void test2(pqxx::connection* conn) {
	pqxx::work transaction(*conn);

	pqxx::result res = transaction.exec("SELECT workchain, shard_id, seqno from blkidext where workchain=-1 order by seqno desc limit 10");
	transaction.commit();

	for (const auto& row : res) {
		std::cout<<'('<<row[0].as<int32_t>()<<','<<std::hex<<row[1].as<int64_t>()<<','<<std::dec<<row[2].as<uint32_t>()<<")\n";
	}	
}