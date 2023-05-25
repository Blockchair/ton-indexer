#include "medium-client.h"

#define DIE_ON_RETRY_MAXOUT(msg) if (retry==0) {std::cerr<<(msg); cleanUpAndDie(1);}

void MediumClient::askForLatest(WalkingMode wmode, int retry) {
	DIE_ON_RETRY_MAXOUT("failed to obtain latest block\n")

	auto b = ton::serialize_tl_object(ton::create_tl_object<ton::lite_api::liteServer_getMasterchainInfo>(), true);
	envelope_send_query(
		std::move(b), 
		[Self = actor_id(this), this, wmode, retry](td::Result<td::BufferSlice> R) -> void {
			if (R.is_error()) {
				sleep(1);
				if (verbosity_ <= Verbosity::DEBUG) {
					std::cout<<"retry latest "<<retry<<'\n';
				}
				return askForLatest(wmode, retry-1);
			}
			auto F = ton::fetch_tl_object<ton::lite_api::liteServer_masterchainInfo>(R.move_as_ok(), true);
			if (F.is_error()) {
				sleep(1);
				if (verbosity_ <= Verbosity::DEBUG) {
					std::cout<<"retry latest "<<retry<<'\n';
				}
				return askForLatest(wmode, retry-1);
			}
			auto f = F.move_as_ok();
			auto blk_id = create_block_id(f->last_);
			td::actor::send_closure_later(Self, &MediumClient::gotLatest, blk_id, wmode);
		}
	);
}

void MediumClient::askForBlkHeader(ton::BlockIdExt blkid, WalkingMode wmode, int retry) {
	DIE_ON_RETRY_MAXOUT("failed to obtain header for " + blkid.to_str() +'\n');

	int mode = 0xffff; // liteAPI's internal magic number
	auto b = ton::serialize_tl_object(ton::create_tl_object<ton::lite_api::liteServer_getBlockHeader>(ton::create_tl_lite_block_id(blkid), mode), true);
	envelope_send_query(
		std::move(b), 
		[Self = actor_id(this), this, requested = blkid, wmode, retry](td::Result<td::BufferSlice> R) mutable -> void {
			if (R.is_error()) {
				if (verbosity_ <= Verbosity::DEBUG) {
					std::cout<<"retry header "<<retry<<'\n';
				}
				return askForBlkHeader(requested, wmode, retry-1);
			}
			auto F = ton::fetch_tl_object<ton::lite_api::liteServer_blockHeader>(R.move_as_ok(), true);
			if (F.is_error()) {
				if (verbosity_ <= Verbosity::DEBUG) {
					std::cout<<"retry header "<<retry<<'\n';
				}
				return askForBlkHeader(requested, wmode, retry-1);
			}
			auto f = F.move_as_ok();
			auto blk_id = ton::create_block_id(f->id_);
			if (blk_id != requested) {
				if (verbosity_ <= Verbosity::DEBUG) {
					std::cout<<"retry header "<<retry<<'\n';
				}
				return askForBlkHeader(requested, wmode, retry-1);
			}
			auto root = vm::std_boc_deserialize(std::move(f->header_proof_));
			if (root.is_error()) {
				if (verbosity_ <= Verbosity::DEBUG) {
					std::cout<<"retry header "<<retry<<'\n';
				}
				return askForBlkHeader(blk_id, wmode, retry-1);
			}
			td::actor::send_closure_later(Self, &MediumClient::gotBlkHeader, blk_id, root.move_as_ok(), wmode);
		}
	);
}

void MediumClient::askForShards(ton::BlockIdExt blkid, WalkingMode wmode, int retry) {
	DIE_ON_RETRY_MAXOUT("failed to obtain shard info from " + blkid.to_str() + '\n');

	auto b = ton::serialize_tl_object(ton::create_tl_object<ton::lite_api::liteServer_getAllShardsInfo>(ton::create_tl_lite_block_id(blkid)), true);
	envelope_send_query(
		std::move(b), 
		[Self = actor_id(this), this, blkid, wmode, retry](td::Result<td::BufferSlice> R) -> void {
			if (R.is_error()) {
				if (verbosity_ <= Verbosity::DEBUG) {
					std::cout<<"retry shards "<<retry<<'\n';
				}
				return askForShards(blkid, wmode, retry-1);
			}
			auto F = ton::fetch_tl_object<ton::lite_api::liteServer_allShardsInfo>(R.move_as_ok(), true);
			if (F.is_error()) {
				if (verbosity_ <= Verbosity::DEBUG) {
					std::cout<<"retry shards "<<retry<<'\n';
				}
				return askForShards(blkid, wmode, retry-1);
			}
			auto f = F.move_as_ok();
			td::actor::send_closure_later(Self, &MediumClient::gotShards, 
				ton::create_block_id(f->id_), 
				std::move(f->data_),
				wmode
			);
		}
	);
}

void MediumClient::askForBlkTransactions(ton::BlockIdExt blk_id, TxFetchMode mode, ton::Bits256 acc_addr, ton::LogicalTime lt, WalkingMode wmode, int retry) {
	DIE_ON_RETRY_MAXOUT("failed to obtain transaction info for block " + blk_id.to_str() +'\n');

	ton::tl_object_ptr<ton::lite_api::liteServer_transactionId3> query_inner = nullptr;
	if (mode == TxFetchMode::REPEAT) {
		query_inner = ton::create_tl_object<ton::lite_api::liteServer_transactionId3>(acc_addr, lt);
	}

	auto query = ton::serialize_tl_object(
		ton::create_tl_object<ton::lite_api::liteServer_listBlockTransactions>(
			ton::create_tl_lite_block_id(blk_id), 7 + mode, 1024, std::move(query_inner), false, false
		),
		true
	);

	envelope_send_query(
		std::move(query), 
		[Self = actor_id(this), this, blk_id, mode, acc_addr, lt, wmode, retry](td::Result<td::BufferSlice> R) {
			if (R.is_error()) {
				if (verbosity_ <= Verbosity::DEBUG) {
					std::cout<<"retry blk tx "<<retry<<'\n';
				}
				return askForBlkTransactions(blk_id, mode, acc_addr, lt, wmode, retry-1);
			}
			td::actor::send_closure_later(Self, &MediumClient::gotBlkTransactions, blk_id, R.move_as_ok(), wmode);
		}
	);
}
void MediumClient::askForTransactionData(ton::BlockIdExt blk_id, BlockData* block_data_ptr, TransactionDescription* tx_descr_ptr, WalkingMode wmode, int retry) {
	DIE_ON_RETRY_MAXOUT("failed to obtain transaction data for block " + blk_id.to_str() +'\n');

	auto a = ton::create_tl_object<ton::lite_api::liteServer_accountId>(blk_id.id.workchain, tx_descr_ptr->addr.addr);
	auto b = ton::create_tl_object<ton::lite_api::liteServer_getOneTransaction>(ton::create_tl_lite_block_id(blk_id), std::move(a), tx_descr_ptr->lt);
	auto c = ton::serialize_tl_object(b, true);

	envelope_send_query(
		std::move(c), 
		[Self = actor_id(this), this, blk_id, block_data_ptr, tx_descr_ptr, wmode, retry](td::Result<td::BufferSlice> R) -> void {
			if (R.is_error()) {
				if (verbosity_ <= Verbosity::DEBUG) {
					std::cout<<"retry tx data "<<retry<<'\n';
				}
				return askForTransactionData(blk_id, block_data_ptr, tx_descr_ptr, wmode, retry-1);
			}
			auto F = ton::fetch_tl_object<ton::lite_api::liteServer_transactionInfo>(R.move_as_ok(), true);
			if (F.is_error()) {
				if (verbosity_ <= Verbosity::DEBUG) {
					std::cout<<"retry tx data "<<retry<<'\n';
				}
				return askForTransactionData(blk_id, block_data_ptr, tx_descr_ptr, wmode, retry-1);
			}
			auto f = F.move_as_ok();
			if (f->transaction_.empty()) { // TODO, remove when I am 200% certain this never happens in normal run
				if (verbosity_ <= Verbosity::DEBUG) {			
					std::cout<<"retry tx empty "<<retry<<" / "<<block_data_ptr->transaction_descriptions.size()<<" / "<<block_data_ptr->this_block.to_str()<<'\n';
				}
				td::actor::send_closure_later(Self, &MediumClient::askForTransactionData, blk_id, block_data_ptr, tx_descr_ptr, wmode, retry-1);
				return;
			}
			td::actor::send_closure_later(Self, &MediumClient::gotTransactionData, 
				blk_id,
				std::move(f->transaction_),
				&(tx_descr_ptr->blob),
				block_data_ptr,
				wmode
			);
		}
	);
}

bool MediumClient::envelope_send_query(td::BufferSlice query, td::Promise<td::BufferSlice> promise) {
	if (!ready_ || client_.empty()) {
		got_result(td::Status::Error("failed to send query to server: not ready"), std::move(promise));
		return false;
	}
	auto P = td::PromiseCreator::lambda(
		[SelfId = actor_id(this), promise = std::move(promise)](td::Result<td::BufferSlice> R) mutable {
		td::actor::send_closure(SelfId, &MediumClient::got_result, std::move(R), std::move(promise));
		}
	);
	td::BufferSlice b = ton::serialize_tl_object(ton::create_tl_object<ton::lite_api::liteServer_query>(std::move(query)), true);
	td::actor::send_closure(client_, &ton::adnl::AdnlExtClient::send_query, "query", std::move(b), td::Timestamp::in(30.0), std::move(P));
	return true;
}

void MediumClient::got_result(td::Result<td::BufferSlice> R, td::Promise<td::BufferSlice> promise) {
	if (R.is_error()) {
		auto err = R.move_as_error();
		LOG(ERROR) << "failed query: " << err;
		promise.set_error(std::move(err));
		return;
	}
	auto data = R.move_as_ok();
	auto F = ton::fetch_tl_object<ton::lite_api::liteServer_error>(data.clone(), true);
	if (F.is_ok()) {
		auto f = F.move_as_ok();
		auto err = td::Status::Error(f->code_, f->message_);
		LOG(ERROR) << "liteserver error: " << err;
		promise.set_error(std::move(err));
		return;
	}
	promise.set_result(std::move(data));
}