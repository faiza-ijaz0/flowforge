# services/workers (placeholder)

Reserved for the Phase 2 standalone worker process deployable: a process that registers itself
against `persistence::IWorkerRepository` (currently `GET /api/v1/workers` always returns an empty
list because nothing writes to this repository yet), heartbeats, and executes dispatched jobs via an
`IExecutor` implementation.

The concrete concurrency primitive this will run on (`flowforge::engine::ThreadPool`) already exists
and is tested/benchmarked — see `engine/include/flowforge/engine/thread_pool.hpp` and
`benchmarks/thread_pool_benchmark.cpp`.
