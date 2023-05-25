#include "medium-client.h"

#include "td/utils/OptionParser.h"

int main(int argc, char** argv) {
	
	if (argc != 2) {
		std::cout<<"gimme blob to test\n";
		return 1;
	}
	auto hex_str = std::string(argv[1]);
	auto blob = td::hex_decode(td::Slice(hex_str)).move_as_ok();

	auto transaction = td::BufferSlice(blob.c_str(), blob.size());

	auto R = vm::std_boc_deserialize(std::move(transaction));
	if (R.is_error()) {
		std::cout<<"failed to deserialize blob as a ton transaction\n";
		return 1;
	}
	// this is a nigh-perfect copypasta from MediumClient::processBlockTransactions
	td::Ref<vm::Cell> root = R.move_as_ok();
	std::vector<vm::CellSlice> msg_bodies_in, msg_bodies_out;
	std::vector<CommonMsgInfo> msg_info_in, msg_info_out;
	if (!unpackMsgsCommonInfo(root, msg_info_in, msg_info_out)) {
		std::cout<<"failed to unpack common info\n";
		return 1;
	}
	auto have_bodies = unpackMsgsBodies(root, msg_bodies_in, msg_bodies_out);

	std::cout<<"in msg:\n";
	for (auto tx : msg_info_in) {
		std::cout<<'\t'<<tx.src.addr.to_hex()<<"\n\t"<<tx.dst.addr.to_hex()<<"\n\t$"<<tx.grams.to_dec_string()<<"\n\t@"<<tx.lt<<"\n";
		if (have_bodies) {
			std::cout<<'\t'<<msg_bodies_in[0].as_bitslice().to_hex()<<'\n';
		}
	}

	std::cout<<"out msgs:\n";
	int i = 0;
	for (auto tx : msg_info_in) {
		std::cout<<(++i)<<")\t"<<tx.src.addr.to_hex()<<"\n\t"<<tx.dst.addr.to_hex()<<"\n\t$"<<tx.grams.to_dec_string()<<"\n\t@"<<tx.lt<<"\n";
		std::cout<<'\t'<<msg_bodies_out[i-1].as_bitslice().to_hex()<<'\n';
	}

	// stuff-to-be-tested-goes-here
	ElectorResponseState debug_ret = {(ElectorOP)0, (ElectorATag)0};
	auto dummy_tx_hash = td::Bits256();

	auto status = processMsg_electorContract(dummy_tx_hash, msg_info_in, msg_info_out, msg_bodies_in, msg_bodies_out, &debug_ret, nullptr);
	if (status != MSG_FunctionsRetCode::OK) {
		std::cout<<"no relevant elector-related activity\n";
		return 0;
	}
	std::cout<<"op code: 0x"<<std::hex<<debug_ret.op;
	switch (debug_ret.op) {
		case ElectorOP::NEW_STAKE:
			std::cout<<" (new stake)";
			break;
		case ElectorOP::RECOVER_STAKE:
			std::cout<<" (recover stake)";
			break;
		default:
			std::cout<<" (?)";
			break;
	}

	std::cout<<" | ans tag: 0x"<<debug_ret.tag;
	switch (debug_ret.tag) {
		case ElectorATag::NEW_STAKE_OK:
		case ElectorATag::RECOVER_STAKE_OK:
			std::cout<<" (OK)";
			break;
		case ElectorATag::NEW_STAKE_FAIL:
		case ElectorATag::RECOVER_STAKE_FAIL:
			std::cout<<" (FAIL)";
			break;
		default:
			std::cout<<" (?)";
			break;
	}
	std::cout<<'\n';
	return 0;
}