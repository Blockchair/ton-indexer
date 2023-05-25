## About

This file is a high level -ish overview of ideas behind ton-indexer and its workflow.

## 3 Main branches

Indexer utilizes 3 main independent "branches" to cover the entirety of TON blockchain: UP, DOWN and MID. Fundamentally they all do the same thing, as described below. the difference lies in what areas of blockchain they cover:
+ UP continuously processes latest blocks on the blockchain
+ MID handles blocks created during Indexer downtime, i.e. blocks between latest at the time it has been started and latest found in database (naturally, should it be empty, this branch is not needed and won't be run)
+ DOWN process everything else, starting at earliest known blocks until creation the blockchain

## Main loop

In principle, under the hood lies LIFO linearization of DFS, except instead of searching for a specific node we are explicitly visiting all of them.

To avoid edge cases arising from asynchronous nature of TON, Indexer leverages the fact that all workchains eventually report their activity to the masterchain: by asking for shards at masterchain heights N and N-K, we generate the "starting row" (N) and "finish line" (N-K) and apply DFS-based tree traversal on the starting row:
1. Pop next block from stack
2. Process it
3. Put its predecessors onto the stack if they are not part of the "finish line"
4. Do so until the stack is empty
5. And when it is, process another K masterchain blocks
6. Reanchor start and finish at N-K and N-2K respectively
7. Repeat until you run out of blocks

## Notes

+ Since TON is a DAG, and not a tree-proper, when 2 branches converge at the same predecessor (aftersplit==true) only one has to be put onto to-process stack, to avoid data duplication. due to the symmetry of the problem, it was decided to truncate the right child (e.g. when blocks on shards 0x40 and 0xC0 share the same predecessor on 0x80, 0x40's predecessor will be put on the stack, while 0xC0's won’t)
