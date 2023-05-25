#include "medium-client.h"

std::unique_ptr<ton::adnl::AdnlExtClient::Callback> MediumClient::make_callback() {
	class Callback : public ton::adnl::AdnlExtClient::Callback {
		private:
			td::actor::ActorId<MediumClient> id_;
		public:
			void on_ready() override {
				td::actor::send_closure(id_, &MediumClient::conn_ready);
			}
			void on_stop_ready() override {
				td::actor::send_closure(id_, &MediumClient::conn_closed);
			}
			Callback(td::actor::ActorId<MediumClient> id) : id_(std::move(id)) {
			}
	};
	return std::make_unique<Callback>(actor_id(this));
}

void MediumClient::conn_closed() {
	ready_ = false;
}

void MediumClient::conn_ready() {
	ready_ = true;
	std::cout<<"ready?\n";
	init();
}


void preConfigureWorker(WorkerData& write_into, const std::string& name, const WalkingMode default_wmode) {
	write_into.name = name;
	write_into.cursor_mc.in_worker = &write_into;
	write_into.cursor_wc.in_worker = &write_into;
	write_into.wmode = default_wmode;
}

// inline ?
void MediumClient::updateBlackList(const std::vector<ton::BlockIdExt>& shards, WorkerData& write_into) {
	// cleaning has to be done elsewhere
	write_into.to_ignore.insert(shards.begin(), shards.end());
}

void MediumClient::updateFirstRow(ton::BlockIdExt mc_blk, const std::set<ton::BlockIdExt>& shards, WorkerData& write_into) {
	write_into.start_mc = mc_blk;
	write_into.starting_row.clear();
	for (auto shard_id : shards) {
		
		write_into.starting_row.insert({write_into.wmode & ~WModeFlag::INIT, shard_id});
	}
	write_into.start_ready = true; // not sure if i will need it in the end.
	// maybe ignore first is added here, not sure yet
}

void MediumClient::updateFirstRow(ton::BlockIdExt mc_blk, const std::vector<ton::BlockIdExt>& shards, WorkerData& write_into) {
	write_into.start_mc = mc_blk;
	write_into.starting_row.clear();
	for (auto shard_id : shards) {
		
		write_into.starting_row.insert({write_into.wmode & ~WModeFlag::INIT, shard_id});
	}
	write_into.start_ready = true; // not sure if i will need it in the end.
	// maybe ignore first is added here, not sure yet
}

void MediumClient::updateAnchor(const std::vector<ton::BlockIdExt>& shards, WorkerData& write_into) {// simplified version of anchor shards
	write_into.anchor_state.clear();
	write_into.anchor_short.clear();

	// write_into.todo_stack.c.clear(); -- i can prob just assume that it will be empty

	for (auto& shard_id : shards) {
		write_into.anchor_state.insert(shard_id);
		write_into.anchor_short.insert(shard_id.id);
		// write_into.todo_stack.push({write_into.wmode, shard_id});
	}
	
	if (verbosity_ <= Verbosity::DEBUG) {
		std::cout<<write_into.name<<" anchors\n";
		for (auto shard_id : write_into.anchor_state) {
			std::cout<<"\t@ "<<shard_id.id.to_str()<<'\n';
		}
	}

	write_into.anchor_ready = true; // do i need this?
}

WorkerData* MediumClient::deduceWorker(WalkingMode wmode) {
	if (wmode & WModeFlag::UP) {
		return &worker_up_;
	}
	if (wmode & WModeFlag::MID) {
		return &worker_mid_;
	}
	if (wmode & WModeFlag::DOWN) {
		return &worker_down_;
	}
	if (wmode & WModeFlag::EMPTY) {
		return &worker_empty_;
	}
	return nullptr; // maybe crash? bcz that's not supposed to happen
}

BlockData* MediumClient::deduceCursor(WalkingMode wmode, const ton::BlockIdExt& blk_id) {
	WorkerData* worker_ptr = deduceWorker(wmode);
	if (blk_id.is_masterchain()) {
		return &(worker_ptr->cursor_mc);
	}
	return &(worker_ptr->cursor_wc);
}

bool MediumClient::areWeThereYet(WorkerData& worker, WalkingMode wmode, ton::BlockIdExt blk_to_check) {
	auto& anchor_short = worker.anchor_short;
	auto& anchor_full = worker.anchor_state;

	if (anchor_short.count(blk_to_check.id) > 0 && anchor_full.count(blk_to_check) == 0) {
		std::cerr<<'('<<worker.name<<") found orphan, dying -- they actually exist\n";
		cleanUpAndDie(1); // die on orphans, should do for now, as I was told they dont exist. lets hope thats true
	}
	if (anchor_full.count(blk_to_check) > 0 || blk_to_check.id.seqno == 1) {
		if (verbosity_ <= Verbosity::DEBUG) {
			std::cout<<"\t@chain "<<blk_to_check.id.workchain<<" ("<<worker.name<<") reached snapshot; branch done " <<worker.todo_stack.size()<<" still in the queue\n"; // should i terminate? i may be possible that only one of the predisessors is in set	
		}
		return true;
	}
	return false;
}

void MediumClient::enqueueStep(WorkerData* worker_ptr) {
	if ((worker_ptr->anchor_ready && worker_ptr->start_ready) == false) {
		return td::actor::send_closure_later(actor_id(this), &MediumClient::enqueueStep, worker_ptr);
	}
	for (const auto& blk_req : worker_ptr->starting_row) {
		worker_ptr->todo_stack.push(blk_req);
	}

	workerStep(worker_ptr);
}

void MediumClient::workerStepTrace(WorkerData* worker_ptr) {
	std::cout<<"ignoring:\n";
	for (const auto& ge : worker_ptr->to_ignore) {
		std::cout<<"\t"<<ge.id.to_str()<<"\n";
	}
	auto dummy = worker_ptr->todo_stack;
	std::cout<<"on todo:\n";
	while (!dummy.empty()) {
		auto ge = dummy.top();
		std::cout<<"\t"<<ge.blk_id.id.to_str()<<' '<<wmode2human(ge.wmode)<<'\n';
		dummy.pop();
	}
}

void MediumClient::masterStep(WorkerData* worker_ptr) {
	if (worker_ptr->master_finished) {
		worker_ptr->todo_stack = {};
	}
	if (worker_ptr->todo_stack.empty()) {
		return enqueueStep(worker_ptr);
	}

	BlockRequest next_to_ask = worker_ptr->todo_stack.top();
	worker_ptr->todo_stack.pop();
	askForBlkHeader(next_to_ask.blk_id, next_to_ask.wmode, max_retry_);	
}

void MediumClient::workerStep(WorkerData* worker_ptr) {
	if (verbosity_ <= Verbosity::DEBUG) {
		workerStepTrace(worker_ptr);
	}
	if (worker_ptr->todo_stack.empty()) {
		if (verbosity_ <= Verbosity::DEBUG) {
			std::cout<<wmode2human(worker_ptr->wmode)<<" insertion prep\n";
			for (const auto& ge : worker_ptr->to_insert) {
				std::cout<<"\t|"<<ge.this_block.id.to_str()<<'\n';
			}
		}

		maybeWriteDB(*worker_ptr, insert_batchsize_, true);
		// restart worker -- maybe into a function? with variation around which branch does what
		worker_ptr->master_finished = false;
		worker_ptr->anchor_ready = false;
		worker_ptr->start_ready = false;
		worker_ptr->mc_step_counter = 0;
		worker_ptr->to_ignore.clear();

		if (worker_ptr->terminal_reached == false) {
			updateFirstRow(worker_ptr->cursor_mc.this_block, worker_ptr->anchor_state, *worker_ptr);
			// return askForBlkHeader(worker_ptr->cursor_mc.previous_blocks[0], worker_ptr->wmode, max_retry_);
			return askForBlkHeader(worker_ptr->cursor_mc.this_block, worker_ptr->wmode, max_retry_);
		}
		if (worker_ptr->wmode & WModeFlag::UP) {
			worker_ptr->terminal = worker_ptr->start_mc.id;
			worker_ptr->terminal_reached = false;
			return askForLatest(WModeFlag::UP | WModeFlag::INIT, max_retry_);				
		}
		if (worker_ptr->wmode & WModeFlag::MID) {
			std::cout<<now()<<worker_ptr->name<<" done\n";
			return;			
		}
		if (worker_ptr->wmode & WModeFlag::DOWN) {
			std::cout<<now()<<worker_ptr->name<<" done\n"; // test if need something special there
			return;			
		}
		if (worker_ptr->wmode & WModeFlag::EMPTY) {
			return init();
		}
	}
	BlockRequest next_to_ask = worker_ptr->todo_stack.top();
	worker_ptr->todo_stack.pop();

	if (next_to_ask.wmode & WModeFlag::NOFIRST) {
		return askForBlkHeader(next_to_ask.blk_id, next_to_ask.wmode, max_retry_);
	}

	if (worker_ptr->to_ignore.count(next_to_ask.blk_id) > 0) {
		return td::actor::send_closure_later(actor_id(this), &MediumClient::workerStep, worker_ptr);
	}
	askForBlkHeader(next_to_ask.blk_id, next_to_ask.wmode, max_retry_);
}

void MediumClient::cleanUpAndDie(int code) {
	delete(database_);

	exit(code);
}