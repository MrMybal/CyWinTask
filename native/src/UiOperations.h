#pragma once
#include <memory>
class TaskService;
class UiBridge;
void registerUiOperations(TaskService&, const std::shared_ptr<UiBridge>&);
