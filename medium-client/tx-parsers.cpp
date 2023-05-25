#include "tx-parsers.h"

bool unpackMsgsCommonInfo(td::Ref<vm::Cell> tx_root, std::vector<CommonMsgInfo>& in_msgs_ret, std::vector<CommonMsgInfo>& out_msgs_ret) {
	block::gen::Transaction::Record transaction_record;
	if (!tlb::unpack_cell(tx_root, transaction_record)) {
		return false; // not sure why it may break, but still
	}
	td::Ref<vm::Cell> in_msg = transaction_record.r1.in_msg->prefetch_ref();
	CommonMsgInfo ret;
	if (in_msg.not_null()) {
		if (!tryUnpackCommonMessageInfo(in_msg, ret)) {
			return false;
		}
		in_msgs_ret.push_back(ret);
	}

	if (transaction_record.outmsg_cnt == 0) {
		return true;
	}
	vm::Dictionary omsg_dict{transaction_record.r1.out_msgs, 15};
	for (int i = 0; i < transaction_record.outmsg_cnt; ++i) {
		vm::CellSlice out_body;
		td::Ref<vm::Cell> omsg_ref = omsg_dict.lookup_ref(td::BitArray<15>{i});
		if (!tryUnpackCommonMessageInfo(omsg_ref, ret)) {
			return false;
		}
		out_msgs_ret.push_back(ret);
	}
	return true;
}

bool unpackMsgsBodies(td::Ref<vm::Cell> tx_root, std::vector<vm::CellSlice>& in_msgs_ret, std::vector<vm::CellSlice>& out_msgs_ret) {
	block::gen::Transaction::Record transaction_record;
	if (!tlb::unpack_cell(tx_root, transaction_record)) {
		return false; // not sure why it may break, but still
	}
	vm::CellSlice carrier = *(transaction_record.r1.in_msg);

	block::gen::Maybe MaybeMessageRef{block::gen::t_Ref_Message_Any};
	block::gen::Maybe::Record_just maybe_message_record;

	auto maybe_in = MaybeMessageRef.unpack(carrier, maybe_message_record);
	if (maybe_in) {
		block::gen::RefT MessageRef{block::gen::t_Message_Any};
		block::gen::MessageAny::Record message_record;

		carrier = *(maybe_message_record.value);
		
		td::Ref<vm::CellSlice> temp;
		MessageRef.fetch_to(carrier, temp);
		carrier = *(temp);

		vm::CellSlice in_body;
		tryUnpackMsgBody(carrier.prefetch_ref(), in_body);
		in_msgs_ret.push_back(std::move(in_body));
	}
	
	if (transaction_record.outmsg_cnt == 0) {
		return true;
	}

	vm::Dictionary omsg_dict{transaction_record.r1.out_msgs, 15};
	for (int i = 0; i < transaction_record.outmsg_cnt; ++i) {
		vm::CellSlice out_body;
		td::Ref<vm::Cell> omsg_ref = omsg_dict.lookup_ref(td::BitArray<15>{i});
		tryUnpackMsgBody(omsg_ref, out_body);
		out_msgs_ret.push_back(std::move(out_body));
	}

	return true;
}

bool tryUnpackMsgBody(td::Ref<vm::Cell> msg_ref, vm::CellSlice& ret) {
	block::gen::Message::Record tmp;

	block::gen::Message MessageAny{block::gen::t_Message_Any};
	if (!MessageAny.cell_unpack(msg_ref, tmp)) {
		return false;
	}
	if (tmp.body.is_null()) {
		return false;
	}
	vm::CellSlice _body = *(tmp.body);
	if (!_body.have(1)) {
		return false;
	}

	if (_body.fetch_long(1) == 0) { // either->LEFT ie msg as is
		ret = *(_body.fetch_subslice(_body.size(), 0));
		return true;
	}
	// else, either->RIGHT ie msg as a reference
	td::Ref<vm::Cell> right = _body.fetch_ref();

	vm::CellSlice rbody{vm::NoVmOrd(), right};
	ret = std::move(rbody);

	return true;
}
bool tryUnpackCommonMessageInfo(td::Ref<vm::Cell> msg_root, CommonMsgInfo& write_into) {
	vm::CellSlice root_cs{vm::NoVmOrd(), msg_root};

	switch (block::gen::t_CommonMsgInfo.get_tag(root_cs)) {
		case block::gen::CommonMsgInfo::ext_in_msg_info: {
			block::gen::CommonMsgInfo::Record_ext_in_msg_info info;
			if (!tlb::unpack(root_cs, info)) {
				return false;
			}
			unpackAddressCellSlice(info.src, write_into.src); // the-void?
			unpackAddressCellSlice(info.dest, write_into.dst);
			write_into.grams = td::BigInt256(0);
			write_into.lt = 0; // not specified, so might as well define as 0

			// info.import_fee; // fees, maybe sometime later
			return true;
		}

		case block::gen::CommonMsgInfo::ext_out_msg_info: {
			block::gen::CommonMsgInfo::Record_ext_out_msg_info info;
			if (!tlb::unpack(root_cs, info)) {
				return false;
			}
			unpackAddressCellSlice(info.src, write_into.src);
			unpackAddressCellSlice(info.dest, write_into.dst); // the-void?
			write_into.grams = td::BigInt256(0);
			write_into.lt = info.created_lt;

			// info.import_fee; // maybe later
			return true;
		}

		case block::gen::CommonMsgInfo::int_msg_info: {
			block::gen::CommonMsgInfo::Record_int_msg_info info;
			if (!tlb::unpack(root_cs, info)) {
				return false;
			}
			td::RefInt256 value;
			td::Ref<vm::Cell> extra;
			if (!block::unpack_CurrencyCollection(info.value, value, extra)) {
				return false;
			}
			
			unpackAddressCellSlice(info.src, write_into.src);
			unpackAddressCellSlice(info.dest, write_into.dst);
			write_into.grams = *value;
			write_into.lt = info.created_lt;
			
			// fees & extra-cur-colls
			return true;
		}

		default:
			return false;	
	}
}

bool unpackAddressCellSlice(td::Ref<vm::CellSlice> address_blob, block::StdAddress& std_addr) {
	ton::WorkchainId wc;
	ton::StdSmcAddress addr;
	if (!block::tlb::t_MsgAddressInt.extract_std_address(address_blob, wc, addr)) {
		return false;
	}
	std_addr = block::StdAddress{wc, addr};
	return true;
}

void debugPrintHex(vm::CellSlice cell) {
	for (unsigned i = 0; i < cell.size(); ++i) {
		printf("%02x", (cell.data()[i] & 0xff));
	}
	if (!cell.empty()) {
		std::cout<<'\n';
	}
}

MSG_FunctionsRetCode processMsg_electorContract(const ton::Bits256& tx_hash, const std::vector<CommonMsgInfo>& imsgs_info, const std::vector<CommonMsgInfo>& omsgs_info, const std::vector<vm::CellSlice>& imsgs_body, const std::vector<vm::CellSlice>& omsgs_body, void* return_ptr, pqxx::connection* db_conn) {
	if (imsgs_info.size() != 1 || omsgs_info.size() != 1) { // needs exactly 1 in and 1 out msgs, as per specification of the contract
		return MSG_FunctionsRetCode::NOTHING;
	}
	if (!(imsgs_info[0].dst == ELECTOR_CONTRACT)) { // may not be the smartest idea to hard-code validator address, but i dont see it changing any time soon
		return MSG_FunctionsRetCode::NOTHING;
	}

	vm::CellSlice in_msg = imsgs_body[0].clone();
	vm::CellSlice out_msg = omsgs_body[0].clone();
	if (!in_msg.have(32 + 64) || !out_msg.have(32 + 64)) {
		return MSG_FunctionsRetCode::ERR;
	}
	
	uint32_t op = (uint32_t)in_msg.fetch_ulong(32);
	uint64_t query_id_in = in_msg.fetch_ulong(64);

	uint32_t ans_tag = (uint32_t)out_msg.fetch_ulong(32);
	uint64_t query_id_out = out_msg.fetch_ulong(64);

	if (return_ptr != nullptr) {
		((ElectorResponseState*)return_ptr)->op = (ElectorOP)op;
		((ElectorResponseState*)return_ptr)->tag = (ElectorATag)ans_tag;
	}

	if (query_id_in != query_id_out) { // mismatched response, that should NOT happen but we are f'd big time if it does
		return MSG_FunctionsRetCode::ERR;
	}
	switch (op) {
		case ElectorOP::TRANSFER: // simple transfer; nothing of value, same as empty body ignored b4
			return MSG_FunctionsRetCode::NOTHING;
		case ElectorOP::NEW_STAKE: // new stake
			return elector_processNewStake(tx_hash, imsgs_info[0].src, imsgs_info[0].grams, imsgs_info[0].lt, in_msg, ans_tag, db_conn);
		case ElectorOP::RECOVER_STAKE: // recover stake request
			return elector_recoverStake(tx_hash, imsgs_info[0].src, omsgs_info[0].grams, omsgs_info[0].lt, in_msg, ans_tag, db_conn);
		case ElectorOP::UPGRADE_CODE: // upgrade code; not needed atm
		case ElectorOP::CONFIRMATION_CONFIG_1: // confirmation from config; not needed atm
		case ElectorOP::CONFIRMATION_CONFIG_2: // also confirm from config; not needed atm
		case ElectorOP::NEW_COMPLAINT: // new complaint; not needed atm
		case ElectorOP::COMPAINT_VOTE: // vote for complaint; not needed atm
			return MSG_FunctionsRetCode::NOIMPL; // not implemented
		default:
			if (!(op & (1 << 31))) { // unknown -- bounce (?)
				return MSG_FunctionsRetCode::ERR;
			}
			// ignore everything else
			return MSG_FunctionsRetCode::ERR;
	} 
}

// https://github.com/ton-blockchain/ton/blob/master/crypto/smartcont/elector-code.fc#L198
MSG_FunctionsRetCode elector_processNewStake(const ton::Bits256& tx_hash, const block::StdAddress& sender, const td::BigInt256& grams, const ton::LogicalTime& lt, vm::CellSlice& in_msg, const uint32_t ans_tag, pqxx::connection* db_conn) {
	if (ans_tag == ElectorATag::NEW_STAKE_FAIL) { // incorrect payload - stake returned
		return MSG_FunctionsRetCode::NOTHING; // not interested in those
	}

	if (ans_tag != ElectorATag::NEW_STAKE_OK) { // something funny going on - manual intervention please
		return MSG_FunctionsRetCode::ERR;
	}

	// if contract was happy with payload - so should we. no byte-checking nessesary
	td::BitSlice validator_pubkey = in_msg.fetch_bits(256);
	in_msg.fetch_bits(32 + 32); // dont need next 2 fields
	td::BitSlice validator_adnl_addr = in_msg.fetch_bits(256);

	td::BigInt256 stake = grams;
	td::BigInt256(1000000000);
	stake -= td::BigInt256(1000000000);

	elector_insertEvent(db_conn, "\\x"+tx_hash.to_hex(), "\\x"+sender.addr.to_hex(), "\\x"+validator_pubkey.to_hex(), "\\x"+validator_adnl_addr.to_hex(), stake.to_dec_string(), lt);
	// (there's some funky flow control that can revert after deducing money btw, do i need to worry about it)
	// (sender, validator_pubkey, validator_adnl_addr, stake) -- can be pushed into database
	return MSG_FunctionsRetCode::OK;
}

//https://github.com/ton-blockchain/ton/blob/master/crypto/smartcont/elector-code.fc#L403
MSG_FunctionsRetCode elector_recoverStake(const ton::Bits256& tx_hash, const block::StdAddress& sender, const td::BigInt256& grams, const ton::LogicalTime& lt, vm::CellSlice& in_msg, const uint32_t ans_tag, pqxx::connection* db_conn) {
	if (ans_tag == ElectorATag::RECOVER_STAKE_FAIL) { // incorrect payload -- reverting
		return MSG_FunctionsRetCode::NOTHING; // not interested in those
	}
	if (ans_tag != ElectorATag::RECOVER_STAKE_OK) { // something funny going on - manual intervention please
		return MSG_FunctionsRetCode::ERR; 
	}

	// guy successfully took everything (in out_msg.grams) -- for us that means tuple containing sender is to be terminated
	// the issue is, i will have to somehow fill rest of reduction row
	elector_insertEvent(db_conn, "\\x"+tx_hash.to_hex(), "\\x"+sender.addr.to_hex(), "", "", "-"+grams.to_dec_string(), lt);
	return MSG_FunctionsRetCode::OK;
}

bool elector_insertEvent(pqxx::connection* conn, const std::string& tx_hash, const std::string& sender, const std::string& pubkey, const std::string& adnl, const std::string& stake, const uint64_t lt) {
	if (conn == nullptr) { // i want to be able to disable writes on-demand
		return false;
	}
	pqxx::work transaction(*conn);

	pqxx::result res = transaction.exec(
		"insert into ton_validator_events (tx_hash, account, pubkey, adnl, stake, logical_time)\
		values ('" +
			tx_hash + "', '" +
			sender + "', '" +
			pubkey + "', '" +
			adnl + "', '" +
			stake + "', " +
			std::to_string(lt) +
		") on conflict on constraint val_e_pkey do nothing"
	);
	transaction.commit();
	return true;
}
