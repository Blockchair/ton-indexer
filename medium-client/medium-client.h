#pragma once
#include <unordered_map>
#include <stack>
#include <vector>
#include <set>
#include <tuple>
#include <cstdarg>
#include <ctime>
#include <iomanip>
#include <functional>

#include "auto/tl/ton_api_json.h"
#include "auto/tl/lite_api.hpp"

#include "adnl/adnl-ext-client.h"

#include "block/mc-config.h"

#include "td/actor/actor.h"
#include "td/utils/filesystem.h"
#include "td/utils/Random.h"

#include "ton/ton-types.h"
#include "ton/lite-tl.hpp"

#include "tl-utils/lite-utils.hpp"

#include "vm/boc.h"
#include "vm/cells/MerkleProof.h"


#include "database.h"
#include "tx-parsers.h"

const uint32_t DEFAULT_INSERT_BATCHSIZE = 10;
const int DEFAULT_MAX_RETRY = 20;
const uint32_t DEFAULT_PRINT_EVERY = 100;

const ton::BlockId GENESIS = ton::BlockId(-1, 0x8000000000000000, 1); // not exactly genesis, but reference to block 0 exists while it itself does not - hence, hence i need to manually stop there

enum Verbosity {DEBUG = 0, INFO = 1, ERROR = 2, FATAL = 3, SILENT = 4};

enum WModeFlag {
	EMPTY	= 1 << 0, // "direction" flag, identifies empty-db filling related activities
	UP		= 1 << 1, // direction flag, identifies UP worker related activities
	MID		= 1 << 2, // direction flag, identifies MID worker related activities
	DOWN	= 1 << 3, // direction flag, identifies DOWN worker related activities
	INIT	= 1 << 4, // purpose flag. part of initilization for the next batch
	ANCHOR	= 1 << 5, // purpose flag. initialization of the next "anchor" row
	NOFIRST = 1 << 6, // control flag. should the first block in every shard of a batch is to be ignored
	NOLAST	= 1 << 7, // control flag. should last block in every shard of a batch is ignored during proceessing
	ONCE	= 1 << 8, // control flag. do only one row, readability sugar equivalent to explicitly not setting nofirst/nolast
	NONE	= 0
};

using WalkingMode = int;

// some magic TON constants
enum TxFetchMode {FIRST = 0, REPEAT = 128};

struct BlockData;
struct WorkerData;
struct BlockRequest;

struct TransactionDescription {
	TransactionDescription(block::StdAddress addr, ton::LogicalTime lt, ton::Bits256 hash, BlockData* belongs_to) : 
		addr(addr), lt(lt), hash(hash), in_block(belongs_to) {}
	block::StdAddress addr;
	ton::LogicalTime lt;
	ton::Bits256 hash;
	bool imsg_exists = false;
	CommonMsgInfo imsg;
	td::BufferSlice* blob = nullptr;
	BlockData* in_block;
};

typedef struct BlockData {
	ton::BlockIdExt this_block;
	std::vector<ton::BlockIdExt> previous_blocks;
	std::vector<TransactionDescription> transaction_descriptions;
	uint64_t tx_in_progress;
	WorkerData* in_worker = nullptr;
} BlockData;

typedef struct WorkerData {
	// string for loggin
	std::string name;
	// default mode this worker adheres to
	WalkingMode wmode;
	//currently proceesed block from mc
	BlockData cursor_mc;
	// currently processed block for other chains 
	BlockData cursor_wc;
	// first block on MC in the batch, excluded from set of WC for cleaner transferrence between it and anchors
	ton::BlockIdExt start_mc;
	// block of the first row of the processing batch with corresponding flow control flags (WC only)
	std::set<BlockRequest> starting_row;
	// row upon reaching which finish callback is called
	std::set<ton::BlockIdExt> anchor_state;
	// orphan detection, coupled with anchor_state
	std::set<ton::BlockId> anchor_short;
	// MC block, when its present in anchor-state -- terminal callback is used instead of ordinary
	ton::BlockId terminal;
	// per-worker db-insert-queue, all that was finished
	std::vector<BlockData> to_insert;
	// LIFO of things to process and how to process them. think linearization of recursive DFS
	std::stack<BlockRequest> todo_stack;
	// some workers ignore last blocks in batch, some first - this be help standardize keeping track of those
	std::set<ton::BlockIdExt> to_ignore;

	// flow syncronization tools
	uint32_t mc_step_counter = 0;
	bool master_finished = false;
	bool start_ready = false;
	bool anchor_ready = false;
	bool terminal_reached = false;

} WorkerData;

typedef struct BlockRequest {
	BlockRequest(WalkingMode wmode, ton::BlockIdExt blk_id) : wmode(wmode), blk_id(blk_id) {}
	WalkingMode wmode;
	ton::BlockIdExt blk_id;
	
	bool operator<(const BlockRequest& other) const {
		return blk_id < other.blk_id;
	}

} BlockRequest;

class MediumClient : public td::actor::Actor {
	private:
		uint64_t is_conf_set_ = 0; // basic mask to filter if things were properly set from cli or die 
		std::string db_str_;
		std::string default_config_ = "medium-client.config";
		td::int32 liteserver_idx_ = -1;
		td::IPAddress remote_addr_;
		ton::PublicKey remote_public_key_;
		
		bool ready_ = false;

		bool no_down_ = false;
		bool no_up_mid_ = false;

		uint32_t insert_batchsize_ = DEFAULT_INSERT_BATCHSIZE;
		int32_t max_retry_ = DEFAULT_MAX_RETRY;
		uint32_t print_every_ = DEFAULT_PRINT_EVERY;
		Verbosity verbosity_ = Verbosity::SILENT;
		
		WorkerData worker_up_, worker_down_, worker_mid_, worker_empty_; 	

		// technically redundant, but better have extra 4 bytes in ram then O(n) through a vector on every step of the loop
		ton::BlockSeqno highest_mc_seqno_ = 0;

		// quasi-serial for database insertion and sync between multiple tables
		uint64_t next_db_row_ = 0;

		pqxx::connection* database_ = nullptr;

	public:
		td::actor::ActorOwn<ton::adnl::AdnlExtClient> client_;
		
		void disable_down_worker(bool disable) {
			no_down_ = disable;
		}

		void disable_up_mid_workers(bool disable) {
			no_up_mid_ = disable;
		}

		void set_remote_addr(td::IPAddress addr) {
			remote_addr_ = addr;
			is_conf_set_ |= 1;
		}

		void set_remote_public_key(td::BufferSlice file_name) {
			auto R = [&]() -> td::Result<ton::PublicKey> {
				td::BufferSlice conf_data = td::read_file(file_name.as_slice().str()).move_as_ok();
				return ton::PublicKey::import(conf_data.as_slice());
			}();
			if (R.is_error()) {
				std::cout<<"bad server public key\n";
				exit(1);
			}
			remote_public_key_ = R.move_as_ok();
			is_conf_set_ |= 2;
		}

		void set_from_global_cfg(std::string file_name) {
			auto G = td::read_file(file_name).move_as_ok();
			auto gc_j = td::json_decode(G.as_slice()).move_as_ok();
			ton::ton_api::liteclient_config_global gc;
			ton::ton_api::from_json(gc, gc_j.get_object()).ensure();
			
			auto idx = liteserver_idx_ >= 0 ? liteserver_idx_ : td::Random::fast(0, static_cast<td::int32>(gc.liteservers_.size() - 1));
			auto& cli = gc.liteservers_[idx];
			remote_addr_.init_host_port(td::IPAddress::ipv4_to_str(cli->ip_), cli->port_).ensure();
			is_conf_set_ |= 1;

			remote_public_key_ = ton::PublicKey{cli->id_};
			is_conf_set_ |= 2;
		}

		void set_internal_config(std::string file_name) {
			auto C = td::read_file(file_name).move_as_ok();
			auto IC_j = td::json_decode(C.as_slice()).move_as_ok();

			db_str_ = td::get_json_object_string_field(IC_j.get_object(), "db_conn_string", false).move_as_ok();
			insert_batchsize_ = td::get_json_object_int_field(IC_j.get_object(), "db_write_batchsize", true, DEFAULT_INSERT_BATCHSIZE).move_as_ok();
			max_retry_ = td::get_json_object_int_field(IC_j.get_object(), "adnl_max_retry", true, DEFAULT_MAX_RETRY).move_as_ok();
			print_every_ = td::get_json_object_int_field(IC_j.get_object(), "print_every", true, DEFAULT_PRINT_EVERY).move_as_ok();
			is_conf_set_ |= 4;
		}

		void set_verbosity_lvl(Verbosity lvl) {
			verbosity_ = lvl;
		}
		
		std::unique_ptr<ton::adnl::AdnlExtClient::Callback> make_callback();

		void conn_ready();
		void conn_closed();

		void start();
		void init();

		// node request-response routines
		void askForLatest(WalkingMode wmode, int retry);
		void inline askForLatest_delayed(WalkingMode wmode, int retry) {
			td::actor::send_closure_later(actor_id(this), &MediumClient::askForLatest, wmode, retry);
		}
		void gotLatest(ton::BlockIdExt blk_id, WalkingMode wmode);

		void askForBlkHeader(ton::BlockIdExt blkid, WalkingMode wmode, int retry);
		void gotBlkHeader(ton::BlockIdExt blk_id, td::Ref<vm::Cell> root, WalkingMode wmode);
		void processHeaderMC(ton::BlockIdExt blk_id, const std::vector<ton::BlockIdExt>& prev, WalkingMode wmode);
		void processHeaderWC(ton::BlockIdExt blk_id, const std::vector<ton::BlockIdExt>& prev, bool aftersplit, WalkingMode wmode);

		void askForShards(ton::BlockIdExt blkid, WalkingMode wmode, int retry);
		void gotShards(ton::BlockIdExt blk, td::BufferSlice data, WalkingMode wmode);

		void askForBlkTransactions(ton::BlockIdExt blk_id, TxFetchMode mode, ton::Bits256 acc_addr, ton::LogicalTime lt, WalkingMode wmode, int retry);		
		void inline beginTXparse(ton::BlockIdExt blk_id, WalkingMode wmode) {
			return askForBlkTransactions(blk_id, TxFetchMode::FIRST, ton::Bits256(), static_cast<ton::LogicalTime>(0), wmode, max_retry_);
		}
		void inline continueTXparse(ton::BlockIdExt blk_id, ton::StdSmcAddress addr, ton::LogicalTime lt, WalkingMode wmode) {
			return askForBlkTransactions(blk_id, TxFetchMode::REPEAT, addr, lt, wmode, max_retry_);
		}
		void gotBlkTransactions(ton::BlockIdExt blk_id, td::BufferSlice data, WalkingMode wmode);

		void askForTransactionData(ton::BlockIdExt blk_id, BlockData* block_data_ptr, TransactionDescription* tx_descr_ptr, WalkingMode wmode, int retry);
		void gotTransactionData(ton::BlockIdExt blk_id, td::BufferSlice arrived_blob, td::BufferSlice** write_into, BlockData* block_data_ptr, WalkingMode wmode);

		void processBlock(BlockData* block_data, WalkingMode wmode);
		void processBlockCommon(BlockData* block_data, WalkingMode wmode);

		// variadic component: function counter, function*, ret_value*, function*, ret_value* ...
		void processBlockTransactions(BlockData* block_data, int fn_count, ...);

		bool envelope_send_query(td::BufferSlice query, td::Promise<td::BufferSlice> promise);
		void got_result(td::Result<td::BufferSlice> R, td::Promise<td::BufferSlice> promise);

		void maybeWriteDB(WorkerData& worker, uint32_t threshold, bool force);

		// flow-control
		void masterStep(WorkerData* worker_ptr);
		void workerStep(WorkerData* worker_ptr);
		void inline workerStepMaybe(WorkerData* worker_ptr) {
			if (worker_ptr->anchor_ready && worker_ptr->start_ready) {
				workerStep(worker_ptr);
			}
		}
		void workerStepTrace(WorkerData* worker_ptr);

		bool areWeThereYet(WorkerData& worker, WalkingMode wmode, ton::BlockIdExt blk_to_check);

		WorkerData* deduceWorker(WalkingMode wmode);
		BlockData* deduceCursor(WalkingMode wmode, const ton::BlockIdExt& blk_id);
		
		void updateAnchor(const std::vector<ton::BlockIdExt>& shards, WorkerData& write_into);
		void updateFirstRow(ton::BlockIdExt mc_blk, const std::vector<ton::BlockIdExt>& shards, WorkerData& write_into);
		void updateFirstRow(ton::BlockIdExt mc_blk, const std::set<ton::BlockIdExt>& shards, WorkerData& write_into);
		void updateBlackList(const std::vector<ton::BlockIdExt>& shards, WorkerData& write_into);

		void enqueueStep(WorkerData* worker_ptr);

		void cleanUpAndDie(int code);	
};

void preConfigureWorker(WorkerData& write_into, const std::string& name, const WalkingMode default_wmode);

void inline updateCursor(BlockData& write_into, ton::BlockIdExt blk_id, const std::vector<ton::BlockIdExt>& prev) {
	write_into.this_block = blk_id;
	write_into.previous_blocks = prev;
}


_block_primitive internal2db(const BlockData& blk);

_block_id proper2simple(const ton::BlockIdExt& blkid);
_tx_descr proper2simple(const TransactionDescription& tx);

ton::BlockIdExt simple2proper(const _block_id& simple);
bool is_parent_child(ton::ShardIdFull x, ton::ShardIdFull y);

std::string wmode2human(WalkingMode wmode);
ton::ShardIdFull getSibling(ton::ShardIdFull x);

char now();

#define IS_AFTERSPLIT_LEFT  (aftersplit && blk_id.shard_full().shard < prev[0].shard_full().shard)
#define IS_AFTERSPLIT_RIGHT (aftersplit && blk_id.shard_full().shard > prev[0].shard_full().shard)
#define IS_TIME_2_PRINT (block_data->this_block.id.seqno % print_every_ == 0)
#define IS_UP_WORKER (wmode & WModeFlag::UP)


_block_primitive internal2db(const BlockData& blk) {
	_block_primitive ret;
	ret.blk_id = proper2simple(blk.this_block);
	for (auto tx : blk.transaction_descriptions) {
		_tx_descr x = proper2simple(tx);
		ret.transactions.push_back(x);
	}
	return ret;
}

_block_id proper2simple(const ton::BlockIdExt& blkid) {
	std::string root_hash = blkid.root_hash.to_hex();
	std::transform(root_hash.begin(), root_hash.end(), root_hash.begin(), [](uint8_t c){return std::tolower(c);});
	root_hash = "\\x" + root_hash;

	std::string file_hash = blkid.file_hash.to_hex();
	std::transform(file_hash.begin(), file_hash.end(), file_hash.begin(), [](uint8_t c){return std::tolower(c);});
	file_hash = "\\x" + file_hash;

	return {blkid.id.workchain, blkid.id.seqno, (int64_t)blkid.id.shard, 0, root_hash, file_hash};
}

_tx_descr proper2simple(const TransactionDescription& tx) {
	std::string tx_addr = tx.addr.addr.to_hex();
	std::transform(tx_addr.begin(), tx_addr.end(), tx_addr.begin(), [](uint8_t c){return std::tolower(c);});
	tx_addr = "\\x" + tx_addr;

	std::string tx_hash = tx.hash.to_hex();
	std::transform(tx_hash.begin(), tx_hash.end(), tx_hash.begin(), [](uint8_t c){return std::tolower(c);});
	tx_hash = "\\x" + tx_hash;

	std::string blob = "\\x";
	for (auto c : std::string(tx.blob->data(), tx.blob->size())) {
		char buf[3];
		sprintf(buf, "%02x", (c & 0xff));
		blob += std::string(buf);
	}

	// common message info of msg_in
	std::string in_msg_src;
	std::string in_msg_dst;
	std::string in_msg_grams = "0";
	if (tx.imsg_exists) {
		in_msg_src = "\\x" + tx.imsg.src.addr.to_hex();
		in_msg_dst = "\\x" + tx.imsg.dst.addr.to_hex();
		in_msg_grams = tx.imsg.grams.to_dec_string();
	}

	return {(uint64_t)tx.lt, tx_addr, tx_hash, blob, in_msg_src, in_msg_dst, in_msg_grams};
}

ton::BlockIdExt simple2proper(const _block_id& simple) {
	auto x = ton::RootHash();
	x.from_hex(td::Slice(simple.roothash));
	
	auto y = ton::FileHash();
	y.from_hex(td::Slice(simple.filehash));

	return ton::BlockIdExt(simple.workchain, simple.shard_id, simple.seqno, x, y);
}

// idea is as follows: xor shard_ids to generate diff, and discard artifacts from padding
// X1{0} is a parent of Y1{0}, for some bitstrings X & Y (WLOG len(X)<len(Y)) iff len(X) of leading bits are same
bool is_parent_child(ton::ShardIdFull x, ton::ShardIdFull y) {
	if (x.workchain != y.workchain || ton::shard_pfx_len(x.shard) == ton::shard_pfx_len(y.shard)) {
		return false;
	}
	ton::ShardId shorter = (ton::shard_pfx_len(x.shard) < ton::shard_pfx_len(y.shard)) ? x.shard : y.shard;

	uint64_t diff = (x.shard ^ y.shard) >> (td::count_trailing_zeroes_non_zero64(shorter) + 1);
	return !((bool)diff);
}

// {A}X1{0} >> traling zeros
// {0}{A}X1 ^ 0b10 = {0}{A}(!X)1
// << trailing zeros
ton::ShardIdFull getSibling(ton::ShardIdFull x) {
	if (ton::shard_pfx_len(x.shard) == 0) {
		return x;
	}
	auto shard = x.shard;
	auto trailing = td::count_trailing_zeroes_non_zero64(shard);
	shard = ((shard >> trailing) ^ 2) << trailing;
	return ton::ShardIdFull(x.workchain, shard);
}

std::string wmode2human(WalkingMode wmode) {
	std::string ret;
	if (wmode & WModeFlag::EMPTY) {
		ret += 'E';
	}
	if (wmode & WModeFlag::UP) {
		ret += 'U';
	}
	if (wmode & WModeFlag::MID) {
		ret += 'M';
	}
	if (wmode & WModeFlag::DOWN) {
		ret += 'D';
	}

	if (wmode & WModeFlag::INIT) {
		ret += 'I';
	}
	if (wmode & WModeFlag::ANCHOR) {
		ret += 'A';
	}

	if (wmode & WModeFlag::NOLAST) {
		ret += 'L';
	}
	if (wmode & WModeFlag::NOFIRST) {
		ret += 'F';
	}
	if (wmode & WModeFlag::ONCE) {
		ret += '1';
	}

	return '[' + ret + ']';
}

char now() {
	std::time_t now = std::time(nullptr);
	std::tm now_tm = *std::gmtime(&now);
	std::cout<<std::put_time(&now_tm, "%d-%m-%Y %H:%M:%S");
	return ' ';
}