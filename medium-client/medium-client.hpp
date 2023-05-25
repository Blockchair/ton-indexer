#pragma once
#include "beggars.hpp"
#include "flow-control.hpp"

#include <algorithm>
#include <iostream>

void MediumClient::start() {
	if (!(is_conf_set_ & 0b11)) {
		std::cout<<"need network config to work\n";
		cleanUpAndDie(1);
	}
	if (!(is_conf_set_ & 0b100)) {
		std::cout<<"no internal config provided, trying default\n";
		set_internal_config(default_config_);
	}

	database_ = new pqxx::connection(db_str_); // not a biggest fan of new, but SOMEBODY in their great wisdom deleted the copy and didnt bother overloading the move on-reference, so here we are

	next_db_row_ = getNextCounter(database_);
	
	client_ = ton::adnl::AdnlExtClient::create(ton::adnl::AdnlNodeIdFull{remote_public_key_}, remote_addr_, make_callback());

}

void MediumClient::init() {	
	std::cout<<"init!\n";
	auto high = getMCRow(database_, SelectEdge::HI);

	if (high.seqno == 0) {
		preConfigureWorker(worker_empty_, "E", WModeFlag::ONCE | WModeFlag::EMPTY);
		return askForLatest(WModeFlag::EMPTY | WModeFlag::INIT, max_retry_);
	}

	if (no_up_mid_) {
		std::cout<<"no up&mid branches requested, skipping\n";
	}
	else {
		// up
		preConfigureWorker(worker_up_, "U", WModeFlag::UP | WModeFlag::NOLAST);
		// worker_up_.terminal = simple2proper(high).id; // debuggin when without mid
		askForLatest_delayed(WModeFlag::UP | WModeFlag::INIT, max_retry_);

		// mid
		preConfigureWorker(worker_mid_, "M", WModeFlag::MID | WModeFlag::NOLAST);
		worker_mid_.terminal = simple2proper(high).id;
		askForLatest(WModeFlag::MID | WModeFlag::INIT, max_retry_);
	}

	
	if (no_down_) {
		std::cout<<"no down branch requested, skipping\n";
		return;
	}
	// down
	auto low = getMCRow(database_, SelectEdge::LO);
	ton::BlockIdExt low_mc_start = simple2proper(low);

	preConfigureWorker(worker_down_, "D", WModeFlag::DOWN | WModeFlag::NOFIRST);
	worker_down_.terminal = GENESIS;

	if (low_mc_start.seqno() <= GENESIS.seqno + 1) { // down branch is filled and needs not further updates
		std::cout<<"down branch is already filled, skipping\n";
		return;
	}
	askForBlkHeader(low_mc_start, WModeFlag::DOWN | WModeFlag::INIT | WModeFlag::NOFIRST, max_retry_);
}

void MediumClient::gotLatest(ton::BlockIdExt blk_id, WalkingMode wmode) {
	if (blk_id.id.seqno <= highest_mc_seqno_ + 3) {
		askForLatest_delayed(wmode, max_retry_);
		return;
	}
	
	highest_mc_seqno_ = blk_id.id.seqno;
	std::cout<<now()<<'('<<deduceWorker(wmode)->name<<") "<<blk_id.id.to_str()<<" as LATEST\n";

	if (wmode == (WModeFlag::MID | WModeFlag::INIT)) {
		worker_up_.terminal = blk_id.id;
		
		askForBlkHeader(blk_id, WModeFlag::MID | WModeFlag::INIT | WModeFlag::NOLAST, max_retry_);
		return;
	}

	if (wmode == (WModeFlag::UP | WModeFlag::INIT)) {
		if (worker_up_.terminal.is_valid() == false) {
			return askForLatest_delayed(WModeFlag::UP | WModeFlag::INIT, max_retry_);
		}
		askForBlkHeader(blk_id, WModeFlag::UP | WModeFlag::INIT | WModeFlag::NOLAST, max_retry_);
		return;
	}

	if (wmode == (WModeFlag::EMPTY | WModeFlag::INIT)) {
		worker_empty_.mc_step_counter = insert_batchsize_ - 1;
		worker_empty_.terminal_reached = true;
		askForBlkHeader(blk_id, WModeFlag::EMPTY | WModeFlag::INIT, max_retry_);
		return;
	}

	askForBlkHeader(blk_id, wmode, max_retry_);
}

void MediumClient::gotBlkHeader(ton::BlockIdExt blk_id, td::Ref<vm::Cell> root, WalkingMode wmode) {
	auto virt_root = vm::MerkleProof::virtualize(root, 1);
	ton::BlockIdExt mc_blkid;
	bool after_split = false;
	std::vector<ton::BlockIdExt> prev;
	block::unpack_block_prev_blk_ext(virt_root, blk_id, prev, mc_blkid, after_split);

	if (verbosity_ <= Verbosity::DEBUG) {
		std::cout<<wmode2human(wmode)<<" got block "<<blk_id.id.to_str()<<'\n';
		for (auto p : prev) {
			std::cout<<"\tprev: "<< p.id.to_str() <<'\n';
		}
	}

	BlockData* cursor_ptr = deduceCursor(wmode, blk_id);
	updateCursor(*cursor_ptr, blk_id, prev);

	if (blk_id.is_masterchain()) {
		return processHeaderMC(blk_id ,prev, wmode);
	}
	processHeaderWC(blk_id, prev, after_split, wmode);
}

void MediumClient::processHeaderMC(ton::BlockIdExt blk_id, const std::vector<ton::BlockIdExt>& prev, WalkingMode wmode) {
	WorkerData* worker_ptr = deduceWorker(wmode);

	bool process_this = true;
	bool ask_for_prev = true;
	bool ask_for_shards = false;
	WalkingMode wmode_prev = wmode;
	WalkingMode wmode_shards = wmode;

	if (wmode & WModeFlag::INIT) {
		ask_for_shards = true;
		wmode_prev = wmode & ~WModeFlag::INIT;
	}

	if (wmode & WModeFlag::NOFIRST) {
		process_this = false;
		wmode_prev = wmode_prev & ~WModeFlag::NOFIRST;
	}

	if (blk_id.id == worker_ptr->terminal) {
		worker_ptr->terminal_reached = true;
	}

	if (worker_ptr->terminal_reached || worker_ptr->mc_step_counter > insert_batchsize_) {
		ask_for_shards = true;
		worker_ptr->master_finished = true;
		wmode_shards = wmode_shards | WModeFlag::ANCHOR;
		if (wmode & WModeFlag::NOLAST) {
			ask_for_prev = false;
			process_this = false;
		}
	}

	if (ask_for_shards) {
		askForShards(blk_id, wmode_shards, max_retry_);
	}

	if (ask_for_prev) {
		worker_ptr->todo_stack.push({wmode_prev, prev[0]});
	}

	if (process_this) {
		worker_ptr->mc_step_counter++;
		return beginTXparse(blk_id, wmode);
	}
	masterStep(worker_ptr);
}

void MediumClient::processHeaderWC(ton::BlockIdExt blk_id, const std::vector<ton::BlockIdExt>& prev, bool aftersplit, WalkingMode wmode) {
	WorkerData* worker_ptr = deduceWorker(wmode);

	bool ask_for_prev = true;
	bool process_this = true;

	if (IS_AFTERSPLIT_RIGHT) {
		ask_for_prev = false;
	}

	if (areWeThereYet(*worker_ptr, wmode, blk_id) == true) {
		ask_for_prev = false;
		if (wmode & WModeFlag::NOLAST) {
			process_this = false;
		}
	}

	if (wmode & WModeFlag::NOFIRST) {
		process_this = false;
		wmode = wmode & ~WModeFlag::NOFIRST;
	}

	if (ask_for_prev) {
		for (auto p : prev) {
			worker_ptr->todo_stack.push({wmode, p});
		}		
	}
	if (process_this) {
		return beginTXparse(blk_id, wmode);	
	}
	return workerStep(worker_ptr);
}

void MediumClient::gotShards(ton::BlockIdExt blk, td::BufferSlice data, WalkingMode wmode) {
	auto root = vm::std_boc_deserialize(data.clone()).move_as_ok();
	block::ShardConfig sh_conf;
	sh_conf.unpack(vm::load_cell_slice_ref(root));
	auto ids = sh_conf.get_shard_hash_ids(true);

	std::vector<ton::BlockIdExt> shards;
	for (auto id : ids) {
		auto ref = sh_conf.get_shard_hash(ton::ShardIdFull(id));
		auto shard_id = ref->top_block_id();
		shards.push_back(shard_id);		
	}

	if (verbosity_ <= Verbosity::DEBUG) {
		std::cout<<wmode2human(wmode)<<" got shards\n";
		for (const auto& s : shards) {
			std::cout<<"\t"<<s.id.to_str()<<'\n';
		}
	}

	WorkerData* worker_ptr = deduceWorker(wmode);

	if ((wmode & WModeFlag::INIT) && (wmode & WModeFlag::NOFIRST)) {
		updateBlackList(shards, *worker_ptr);
	}
	if ((wmode & WModeFlag::ANCHOR) && (wmode & WModeFlag::NOLAST)) {
		updateBlackList(shards, *worker_ptr);
	}

	if (wmode & WModeFlag::INIT) {
		updateFirstRow(blk, shards, *worker_ptr);
	}
	if (wmode & WModeFlag::ANCHOR) {
		updateAnchor(shards, *worker_ptr);
	}
}

void MediumClient::gotBlkTransactions(ton::BlockIdExt blk_id, td::BufferSlice data, WalkingMode wmode) {
	auto F = ton::fetch_tl_object<ton::lite_api::liteServer_blockTransactions>(std::move(data), true);
	if (F.is_error()) {
		std::cout<<"???\n";
		cleanUpAndDie(1);
	}
	auto f = F.move_as_ok();

	BlockData* block_data_ptr = deduceCursor(wmode, blk_id);

	std::vector<TransactionDescription>& write_into = block_data_ptr->transaction_descriptions;

	for(std::size_t i = 0; i < f->ids_.size(); i++) {
		auto workchain = blk_id.id.workchain;
		auto account = f->ids_[i]->account_;
		auto logical_time = static_cast<ton::LogicalTime>(f->ids_[i]->lt_);
		auto hash = f->ids_[i]->hash_;
		write_into.emplace_back(block::StdAddress{workchain, account}, logical_time, hash, block_data_ptr); // question, this constructor has some defaults, will that screw us later?
	}

	if (f->incomplete_ && !write_into.empty()) {
		const auto& T = *write_into.rbegin();
		return continueTXparse(blk_id, T.addr.addr, T.lt, wmode);
	}

	if (!f->incomplete_ && !write_into.empty()) {
		block_data_ptr->tx_in_progress = write_into.size();
		for (auto& tx : block_data_ptr->transaction_descriptions) {
			askForTransactionData(blk_id, block_data_ptr, &tx, wmode, max_retry_);
		}
		return;
	}
	processBlock(block_data_ptr, wmode);
}

void MediumClient::gotTransactionData(ton::BlockIdExt blk_id, td::BufferSlice arrived_blob, td::BufferSlice** write_into, BlockData* block_data_ptr, WalkingMode wmode) {
	if ((*write_into) != nullptr) {
		delete(*write_into);
	}
	*write_into = new td::BufferSlice(std::move(arrived_blob));

	--(block_data_ptr->tx_in_progress);

	if (block_data_ptr->tx_in_progress == 0) {
		processBlock(block_data_ptr, wmode);
	}
}

// all the extra magic goes here
void MediumClient::processBlock(BlockData* block_data, WalkingMode wmode) {
	processBlockTransactions(block_data, 1, &processMsg_electorContract, nullptr);
	// processBlockTransactions(block_data, 0);
	processBlockCommon(block_data, wmode);
}

void MediumClient::processBlockTransactions(BlockData* block_data, const int fn_count, ...) {
	for (auto& tx_descr : block_data->transaction_descriptions) {
		auto transaction = (tx_descr.blob)->clone();
		if (transaction.empty()) {
			std::cerr<<"failed to load tx data for deserialization for (addr:hash:lt) "<<tx_descr.addr.addr.to_hex()<<':'<<tx_descr.hash.to_hex()<<':'<<tx_descr.lt<<'\n';
			cleanUpAndDie(1);
		}

		auto R = vm::std_boc_deserialize(std::move(transaction));
		if (R.is_error()) {
			std::cerr<<"failed to deserialize transaction (addr:hash:lt) "<<tx_descr.addr.addr.to_hex()<<':'<<tx_descr.hash.to_hex()<<':'<<tx_descr.lt<<'\n';
			cleanUpAndDie(1);
		}
		td::Ref<vm::Cell> root = R.move_as_ok();
		std::vector<vm::CellSlice> msg_bodies_in, msg_bodies_out;

		std::vector<CommonMsgInfo> msg_info_in, msg_info_out;
		auto status = unpackMsgsCommonInfo(root, msg_info_in, msg_info_out);
		
		if (status && !msg_info_in.empty()) {
			tx_descr.imsg_exists = true;
			tx_descr.imsg = msg_info_in[0];
		}

		status = unpackMsgsBodies(root, msg_bodies_in, msg_bodies_out);
		
		// someone call an exorcist, im enjoying this too much
		va_list var_args;
		va_start(var_args, fn_count);
		for (auto i = 0; i < fn_count; ++i) {		
			auto function = va_arg(var_args, MSG_FunctionsInterface);
			auto* ret_val = va_arg(var_args, void*);
			status = function(tx_descr.hash, msg_info_in, msg_info_out, msg_bodies_in, msg_bodies_out, ret_val, database_);
		}
		va_end(var_args);
	}
}

void MediumClient::processBlockCommon(BlockData* block_data, WalkingMode wmode) { // process block common??
	auto& worker = *(block_data->in_worker);
	if (block_data->this_block.is_masterchain() && (IS_TIME_2_PRINT)) {
		std::cout<<now()<<'('<<worker.name<<") "<<block_data->this_block.id.to_str()<<'\n';
	}

	worker.to_insert.push_back(*block_data);
	block_data->transaction_descriptions.clear();

	if (block_data->this_block.is_masterchain()) {
		return masterStep(&worker);
	}

	return workerStep(&worker);
}

// this stuff has to be protected, but TON runs on GIL, so...
void MediumClient::maybeWriteDB(WorkerData& worker, uint32_t threshold, bool force) {
	// pthread_mutex_lock(&db_lock_);
	if (!force && worker.to_insert.size() < threshold) {
		return;
	}
	std::vector<_block_primitive> x;
	for (auto blk : worker.to_insert) {
		x.push_back(internal2db(blk));
		// lets see if this will backfire
		for (auto& tx_descr : blk.transaction_descriptions) {
			free(tx_descr.blob);
		}
	}
	worker.to_insert.clear();
	try {
		next_db_row_ = insertBlocks(database_, x, next_db_row_);
	}
	catch (const std::exception& ex) {
		std::cout<<"in worker ("<<worker.name<<"): db write error: "<<ex.what()<<"\n";
		cleanUpAndDie(1);
	}
	// pthread_mutex_unlock(&db_lock_);
}
