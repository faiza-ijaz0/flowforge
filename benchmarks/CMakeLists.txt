# Benchmark foundation. Currently covers the concurrency primitives
# (ThreadPool/BlockingQueue) that are actually implemented; benchmarks for
# job throughput/scheduling latency belong here once the scheduler
# (Phase 2) exists.

add_executable(flowforge_benchmarks
  thread_pool_benchmark.cpp
  blocking_queue_benchmark.cpp
  handler_registry_benchmark.cpp
)

target_link_libraries(flowforge_benchmarks
  PRIVATE
    flowforge::engine
    benchmark::benchmark_main
)

flowforge_set_project_warnings(flowforge_benchmarks)
