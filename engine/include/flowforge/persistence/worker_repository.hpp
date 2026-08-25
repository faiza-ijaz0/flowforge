#pragma once

#include <vector>

#include "flowforge/domain/worker.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/result.hpp"

namespace flowforge::persistence {

class IWorkerRepository {
 public:
  IWorkerRepository() = default;
  virtual ~IWorkerRepository() = default;
  IWorkerRepository(const IWorkerRepository&) = delete;
  IWorkerRepository& operator=(const IWorkerRepository&) = delete;
  IWorkerRepository(IWorkerRepository&&) = delete;
  IWorkerRepository& operator=(IWorkerRepository&&) = delete;

  virtual Result<void> insert(const domain::Worker& worker) = 0;
  [[nodiscard]] virtual Result<domain::Worker> find_by_id(const infra::WorkerId& id) const = 0;
  [[nodiscard]] virtual Result<std::vector<domain::Worker>> list() const = 0;
  virtual Result<void> update(const domain::Worker& worker) = 0;
};

}  // namespace flowforge::persistence
