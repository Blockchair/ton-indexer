#pragma once

#include <vector>

#include <pqxx/pqxx>

#include "vm/boc.h"
#include "block/block.h"
#include "block/block-auto.h"
#include "block/block-parse.h"

typedef struct CommonMsgInfo {
	block::StdAddress src;
	block::StdAddress dst;
	td::BigInt256 grams;
	ton::LogicalTime lt;
} CommonMsgInfo;

void debugPrintHex(vm::CellSlice cell);

bool unpackMsgsCommonInfo(td::Ref<vm::Cell> tx_root, std::vector<CommonMsgInfo>& in_msgs_ret, std::vector<CommonMsgInfo>& out_msgs_ret);
bool tryUnpackCommonMessageInfo(td::Ref<vm::Cell> msg_root, CommonMsgInfo& write_into);

bool unpackMsgsBodies(td::Ref<vm::Cell> tx_root, std::vector<vm::CellSlice>& in_msgs_ret, std::vector<vm::CellSlice>& out_msgs_ret);
bool tryUnpackMsgBody(td::Ref<vm::Cell> msg_ref, vm::CellSlice& ret);

bool unpackAddressCellSlice(td::Ref<vm::CellSlice> address_blob, block::StdAddress& std_addr);

enum MSG_FunctionsRetCode {OK = 0, ERR = 1, NOTHING = 2, NOIMPL = 3};

// using MSG_FunctionsInterface = std::function<void(const std::vector<CommonMsgInfo>&, const std::vector<CommonMsgInfo>&, const std::vector<vm::CellSlice>&, const std::vector<vm::CellSlice>&, void*, pqxx::connection*)>;
using MSG_FunctionsInterface = MSG_FunctionsRetCode (*)(const ton::Bits256&, const std::vector<CommonMsgInfo>&, const std::vector<CommonMsgInfo>&, const std::vector<vm::CellSlice>&, const std::vector<vm::CellSlice>&, void*, pqxx::connection*);

/*
	quick how-to-write modules for message parsing: 
	1. for the entry-point, please adhere to the following interface:
		args:
			const ton::Bits256& tx_hash, 
			const std::vector<CommonMsgInfo>& imsgs_info, 
			const std::vector<CommonMsgInfo>& omsgs_info, 
			const std::vector<vm::CellSlice>& imsgs_body, 
			const std::vector<vm::CellSlice>& omsgs_body, 
			void* return_ptr, 
			pqxx::connection* db_conn
		returns:
			MSG_FunctionsRetCode
	2. You are free to define return_ptr object as task may demand, or leave it as nullptr if not needed
	3. by default the same db-connector is used for medium-client routine; for testing it may be helpful to pass nullptr
	4. regarding return codes, please adhere to the following: 
		OK = relevant data found and parsing successfully finished
		ERR = relevant data was EXPECTED to be found but parsing was unsuccessful
		NOTHING = false-positive, either opcode indicates not-relevant action or participants are of no-interest
		NOIMPL = not implemented, if some feature is not implemented yet

		please refrain from adding extras, and use return_ptr for fine-tuned error detection if needed
	5. expect that Your entry-point will be called on EVERY transaction medium-client will encounter, early detection of exiting conditions is advised
	6. testing of the flow can be done via transaction-tester executable
	7. integration into medium-client is achieved via processBlockTransactions variadic function 
	8. please refer to validator parsing routine for examples
*/ 

enum ElectorOP {
	TRANSFER = 0,
	NEW_STAKE = 0x4e73744b,
	RECOVER_STAKE = 0x47657424,
	UPGRADE_CODE = 0x4e436f64,
	CONFIRMATION_CONFIG_1 = 0xee764f4b,
	CONFIRMATION_CONFIG_2 = 0xee764f6f,
	NEW_COMPLAINT = 0x52674370,
	COMPAINT_VOTE = 0x56744370
};

enum ElectorATag {
	NEW_STAKE_FAIL = 0xee6f454c,
	NEW_STAKE_OK = 0xf374484c,
	RECOVER_STAKE_FAIL = 0xfffffffe,
	RECOVER_STAKE_OK = 0xf96f7324
};

typedef struct ElectorResponseState {
	ElectorOP op;
	ElectorATag tag;
} ElectorResponseState;

const block::StdAddress ELECTOR_CONTRACT = {-1, td::ConstBitPtr{(const unsigned char*)"\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33"}};

MSG_FunctionsRetCode processMsg_electorContract(const ton::Bits256& tx_hash, const std::vector<CommonMsgInfo>& imsgs_info, const std::vector<CommonMsgInfo>& omsgs_info, const std::vector<vm::CellSlice>& imsgs_body, const std::vector<vm::CellSlice>& omsgs_body, void* return_ptr, pqxx::connection* db_conn);
MSG_FunctionsRetCode elector_processNewStake(const ton::Bits256& tx_hash, const block::StdAddress& sender, const td::BigInt256& grams, const ton::LogicalTime& lt, vm::CellSlice& in_msg, uint32_t ans_tag, pqxx::connection* db_conn);
MSG_FunctionsRetCode elector_recoverStake(const ton::Bits256& tx_hash, const block::StdAddress& sender, const td::BigInt256& grams, const ton::LogicalTime& lt, vm::CellSlice& in_msg, uint32_t ans_tag, pqxx::connection* db_conn);
bool elector_insertEvent(pqxx::connection* conn, const std::string& tx_hash, const std::string& sender, const std::string& pubkey, const std::string& adnl, const std::string& stake, uint64_t lt);
