#include "UiOperations.h"
#include "TaskService.h"
#include "UiBridge.h"
#include <string>

using cymcp::Json;

void registerUiOperations(TaskService& service, const std::shared_ptr<UiBridge>& bridge)
{
    const auto add = [&](const char* name, const char* description, Json properties,
                         Json required, const bool readOnly) {
        if (properties.is_null()) properties = Json::object();
        Json input = {{"type", "object"}, {"properties", std::move(properties)},
                      {"additionalProperties", false}};
        if (!required.empty()) input["required"] = std::move(required);
        cymcp::Operation operation {name, description, std::move(input), {{"type", "object"}},
            readOnly, [bridge, operation = std::string(name)](const Json& args,
                                                           const cymcp::CallContext& context) {
                return bridge->invoke(operation, args, context);
            }};
        service.unrestrictedRegistry()->add(operation);
        service.registry()->add(std::move(operation));
    };
    const Json boolean = {{"type", "boolean"}};
    const Json identity = {{"type", "object"}, {"properties", {
        {"pid", {{"type", "integer"}, {"minimum", 1}}},
        {"created", {{"type", "integer"}, {"minimum", 1}}}
    }}, {"required", {"pid", "created"}}, {"additionalProperties", false}};
    add("ui.state", "Read the current page, process filters, selection, performance layout and detached windows.",
        {}, Json::array(), true);
    add("ui.navigate", "Show the Processes, Performance, Assistant, Console, Temperatures or System page.",
        {{"page", {{"type", "string"}, {"enum", {"processes", "performance", "assistant", "console", "temperatures", "system"}}}},
         {"device_id", {{"type", "string"}, {"maxLength", 128}}},
         {"view", {{"type", "string"}, {"enum", {"graphs", "detail"}}}}},
        {"page"}, false);
    add("ui.processes.configure", "Set the process view, search, type filter, grouping, tree, density, inspector or sorting.",
        {{"view", {{"type", "string"}, {"enum", {"all", "gpu"}}}},
         {"query", {{"type", "string"}, {"maxLength", 255}}},
         {"category", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3}}},
         {"group_by_type", boolean}, {"related", boolean}, {"compact", boolean},
         {"inspector", boolean},
         {"sort_column", {{"type", "integer"}, {"minimum", 0}, {"maximum", 8}}},
         {"descending", boolean}}, Json::array(), false);
    add("ui.processes.select", "Select one or several current processes by PID and creation timestamp.",
        {{"processes", {{"type", "array"}, {"items", identity}}},
         {"mode", {{"type", "string"}, {"enum", {"replace", "add", "remove"}}}}},
        {"processes"}, false);
    add("ui.processes.expand", "Expand or collapse the associated-process branch for a current process.",
        {{"pid", {{"type", "integer"}, {"minimum", 1}}},
         {"created", {{"type", "integer"}, {"minimum", 1}}}, {"expanded", boolean}},
        {"pid", "created", "expanded"}, false);
    add("ui.processes.inspect", "Open the application's advanced inspection report for one current process.",
        {{"pid", {{"type", "integer"}, {"minimum", 1}}},
         {"created", {{"type", "integer"}, {"minimum", 1}}}},
        {"pid", "created"}, false);
    add("ui.processes.trust", "Read the application's local trust indicator for one current process.",
        {{"pid", {{"type", "integer"}, {"minimum", 1}}},
         {"created", {{"type", "integer"}, {"minimum", 1}}}},
        {"pid", "created"}, true);
    add("ui.processes.open_location", "Open Explorer with the selected process executable highlighted.",
        {{"pid", {{"type", "integer"}, {"minimum", 1}}},
         {"created", {{"type", "integer"}, {"minimum", 1}}}},
        {"pid", "created"}, false);
    add("ui.processes.copy", "Copy the selected process path or its name and PID to the clipboard.",
        {{"pid", {{"type", "integer"}, {"minimum", 1}}},
         {"created", {{"type", "integer"}, {"minimum", 1}}},
         {"what", {{"type", "string"}, {"enum", {"path", "name_pid"}}}}},
        {"pid", "created", "what"}, false);
    add("ui.performance.configure", "Set graph or detailed view, row or card layout, columns, compact cards, extra devices and CPU core layout.",
        {{"view", {{"type", "string"}, {"enum", {"graphs", "detail"}}}},
         {"device_id", {{"type", "string"}, {"maxLength", 128}}},
         {"columns", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5}}},
         {"compact_cards", boolean}, {"rows", boolean}, {"detail_cards", boolean}, {"extra_devices", boolean},
         {"show_cores", boolean}, {"compact_cores", boolean}}, Json::array(), false);
    add("ui.performance.history", "Read up to 120 real graph samples for one device metric from the current UI history.",
        {{"device_id", {{"type", "string"}, {"maxLength", 128}}},
         {"metric_id", {{"type", "string"}, {"maxLength", 128}}},
         {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 120}}}},
        {"device_id", "metric_id"}, true);
    add("ui.window.set", "Open or close the detached dashboard or a device graph window.",
        {{"target", {{"type", "string"}, {"maxLength", 128}}}, {"open", boolean}},
        {"target", "open"}, false);
    add("ui.pause", "Pause or resume the application's displayed collector samples.",
        {{"paused", boolean}}, {"paused"}, false);
    add("ui.scale", "Set the application's interface zoom from 100 to 170 percent.",
        {{"percent", {{"type", "integer"}, {"minimum", 100}, {"maximum", 170}}}},
        {"percent"}, false);
}
