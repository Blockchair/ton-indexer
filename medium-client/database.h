#include <pqxx/pqxx>
#include <pqxx/binarystring.hxx>
// #include <pqxx/stream_to.hxx>

#include <tuple>
#include <vector>

using _block_row = std::tuple<uint64_t, int32_t, int64_t, uint64_t, std::string, std::string,  uint64_t>;
using _transaction_row = std::tuple<std::string, std::string, uint64_t, uint64_t, std::string, std::string, std::string, std::string>;

enum SelectEdge {HI, LO};

typedef struct _block_id {
	int32_t workchain;
	uint32_t seqno;
	int64_t shard_id;
	uint64_t id;
	std::string roothash;
	std::string filehash;
	

	bool operator==(const _block_id& other) const {
		return workchain == other.workchain 
			&& seqno == other.seqno 
			&& shard_id == other.shard_id 
			&& roothash == other.roothash 
			&& filehash == other.filehash;
	}

} _block_id;

typedef struct _tx_descr {
	uint64_t lt;
	std::string account;
	std::string hash;
	std::string blob;
	std::string imsg_src;
	std::string imsg_dst;
	std::string grams; // too large for u64, going for str-numeric instead
} _tx_descr;

typedef struct _block_primitive {
	_block_id blk_id;
	std::vector<_tx_descr> transactions;
} _block_primitive;


const std::string sql_get_edge_row_1_ = "select	distinct		\
	b.workchain, b.shard_id, b.seqno, b.roothash, b.filehash	\
from (															\
	select														\
		workchain, shard_id, ";

const std::string sql_minSeqno = "min(seqno)";
const std::string sql_maxSeqno = "max(seqno)";

const std::string sql_get_edge_row_2_ = "	as seqno			\
	from														\
		ton_block												\
	group by													\
		(workchain, shard_id)									\
) as a															\
join (															\
	select														\
		workchain, shard_id, seqno, roothash, filehash			\
	from														\
		ton_block												\
) as b															\
on																\
	a.workchain = b.workchain									\
and																\
	a.shard_id = b.shard_id										\
and																\
	a.seqno = b.seqno";

const std::string sql_get_edge_mc_1_ = "select					\
	workchain, shard_id, seqno, roothash, filehash				\
from 															\
	ton_block													\
where															\
	workchain = -1												\
order by														\
	seqno ";

const std::string sql_desc_ = "desc ";
const std::string sql_asc_ = "asc ";

_block_id getMCRow(pqxx::connection* conn, SelectEdge edge);
std::vector<_block_id> getEdgeRow(pqxx::connection* conn, SelectEdge edge);
_block_id getBlkBy3Keys(pqxx::connection* conn, int32_t workchain, int64_t shard_id, uint32_t seqno);
uint64_t getNextCounter(pqxx::connection* conn);
uint64_t insertBlocks(pqxx::connection* conn, std::vector<_block_primitive>& blocks, uint64_t counter);


void test(pqxx::connection* conn);
void test2(pqxx::connection* conn);