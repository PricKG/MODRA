#include "domain/Task.h"

#include <stdexcept>

namespace modra {

std::string_view task_type_name(TaskType type) {
    switch (type) {
        case TaskType::technical: return "technical";
        case TaskType::administrative: return "administrative";
        case TaskType::management: return "management";
        case TaskType::research: return "research";
        case TaskType::documentation: return "documentation";
        case TaskType::follow_up: return "follow_up";
    }
    throw std::invalid_argument("Tipo de tarea inválido.");
}

std::string_view task_type_label(TaskType type) {
    switch (type) {
        case TaskType::technical: return "Técnica";
        case TaskType::administrative: return "Administrativa";
        case TaskType::management: return "Gestión";
        case TaskType::research: return "Investigación";
        case TaskType::documentation: return "Documentación";
        case TaskType::follow_up: return "Seguimiento";
    }
    throw std::invalid_argument("Tipo de tarea inválido.");
}

TaskType task_type_from_name(std::string_view name) {
    if (name == "technical") return TaskType::technical;
    if (name == "administrative") return TaskType::administrative;
    if (name == "management") return TaskType::management;
    if (name == "research") return TaskType::research;
    if (name == "documentation") return TaskType::documentation;
    if (name == "follow_up") return TaskType::follow_up;
    throw std::runtime_error("SQLite contiene un tipo de tarea inválido: " + std::string(name));
}

std::string_view task_status_name(TaskStatus status) {
    switch (status) {
        case TaskStatus::pending: return "pending";
        case TaskStatus::in_progress: return "in_progress";
        case TaskStatus::blocked: return "blocked";
        case TaskStatus::in_review: return "in_review";
        case TaskStatus::completed: return "completed";
        case TaskStatus::cancelled: return "cancelled";
    }
    throw std::invalid_argument("Estado de tarea inválido.");
}

std::string_view task_status_label(TaskStatus status) {
    switch (status) {
        case TaskStatus::pending: return "Pendiente";
        case TaskStatus::in_progress: return "En curso";
        case TaskStatus::blocked: return "Bloqueada";
        case TaskStatus::in_review: return "En revisión";
        case TaskStatus::completed: return "Finalizada";
        case TaskStatus::cancelled: return "Cancelada";
    }
    throw std::invalid_argument("Estado de tarea inválido.");
}

TaskStatus task_status_from_name(std::string_view name) {
    if (name == "pending") return TaskStatus::pending;
    if (name == "in_progress") return TaskStatus::in_progress;
    if (name == "blocked") return TaskStatus::blocked;
    if (name == "in_review") return TaskStatus::in_review;
    if (name == "completed") return TaskStatus::completed;
    if (name == "cancelled") return TaskStatus::cancelled;
    throw std::runtime_error("SQLite contiene un estado de tarea inválido: " + std::string(name));
}

std::string_view task_priority_name(TaskPriority priority) {
    switch (priority) {
        case TaskPriority::low: return "low";
        case TaskPriority::normal: return "normal";
        case TaskPriority::high: return "high";
        case TaskPriority::critical: return "critical";
    }
    throw std::invalid_argument("Prioridad de tarea inválida.");
}

std::string_view task_priority_label(TaskPriority priority) {
    switch (priority) {
        case TaskPriority::low: return "Baja";
        case TaskPriority::normal: return "Normal";
        case TaskPriority::high: return "Alta";
        case TaskPriority::critical: return "Crítica";
    }
    throw std::invalid_argument("Prioridad de tarea inválida.");
}

TaskPriority task_priority_from_name(std::string_view name) {
    if (name == "low") return TaskPriority::low;
    if (name == "normal") return TaskPriority::normal;
    if (name == "high") return TaskPriority::high;
    if (name == "critical") return TaskPriority::critical;
    throw std::runtime_error("SQLite contiene una prioridad de tarea inválida: " + std::string(name));
}

}  // namespace modra
