#include "medium-client.hpp"

#include "td/utils/OptionParser.h"

int main(int argc, char** argv) {
	SET_VERBOSITY_LEVEL(verbosity_INFO);
	td::actor::ActorOwn<MediumClient> runner;

	td::OptionParser p;
	p.set_description("indexer for TON blockchain");
	p.add_option('h', "help", "how to use this thing", [&]() {
		std::cout<<"Tool for indexing and storing TON blockchain activity into a PostgreSQL database for further processing\n\
Usage: medium-client (-a 0.1.2.3:4 -p pubkey.pub | -g) [-n] [-H] [-C medium-client.config] [-v]\n\
-h | --help                        show this message and die\n\
-a | --addr <ip:port>              get data from a specific ton node, requires pubkey file to be provided\n\
-p | --pub <pubkey-file-name>      pubkey for the handshake, only used with -a\n\
-g | --global                      connect to a random node from ./ton-global.config\n\
-n | --no-down                     do not ask for blocks happened before earlies known to the indexer, can be helpful with non-archival nodes\n\
-H | --historic                    do not ask for blocks happened after latest known to the indexer\n\
-C | --config <config-file-name>   config file for connection/database constants, when not provided will attempt to use ./medium-client.config\n\
-v | --verbose                     set maximum verbosity, prints EVERYTHING. very slow, very cluttered, great for debugging\n";
	exit(0);
	});

	p.add_checked_option('a', "addr", "connect to ip:port", [&](td::Slice arg) {
		td::IPAddress addr;
		TRY_STATUS(addr.init_host_port(arg.str()));
		td::actor::send_closure(runner, &MediumClient::set_remote_addr, addr);
		return td::Status::OK();
	});
	
	p.add_checked_option('p', "pub", "remote public key", [&](td::Slice arg) {
		td::actor::send_closure(runner, &MediumClient::set_remote_public_key, td::BufferSlice{arg});
		return td::Status::OK();
	});

	p.add_option('g', "global", "use global-config", [&]() {
		std::string global_config = "ton-global.config";
		td::actor::send_closure(runner, &MediumClient::set_from_global_cfg, global_config);	
	});

	p.add_option('n', "no-down", "do not parse blockchain down", [&]() {
		td::actor::send_closure(runner, &MediumClient::disable_down_worker, true);
	});
	
	p.add_option('H', "historic", "do not parse blockchain up", [&]() {
		td::actor::send_closure(runner, &MediumClient::disable_up_mid_workers, true);
	});

	p.add_option('C', "config", "internal config file", [&](td::Slice arg) {		
		td::actor::send_closure(runner, &MediumClient::set_internal_config, arg.str());
	});

	p.add_option('v', "verbose", "print everything, mostly debug information", [&]() {
		td::actor::send_closure(runner, &MediumClient::set_verbosity_lvl, Verbosity::DEBUG);
	});

	td::actor::Scheduler scheduler({2});
	
	scheduler.run_in_context([&] {
		runner = td::actor::create_actor<MediumClient>("MediumClient");
	});

	scheduler.run_in_context([&] {p.run(argc, argv).ensure();});
	scheduler.run_in_context([&] {
		td::actor::send_closure(runner, &MediumClient::start);
		runner.release();
	});
	scheduler.run();


	return 0;
}
