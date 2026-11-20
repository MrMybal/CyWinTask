#pragma once
#include <memory>
#include <string>
class UiBridge;

class AssistantPane {
public:
    explicit AssistantPane(bool demo = false, std::shared_ptr<UiBridge> ui = {});
    ~AssistantPane();
    AssistantPane(const AssistantPane&) = delete;
    AssistantPane& operator=(const AssistantPane&) = delete;
    void draw();
    void drawConfirmation();
    bool allows(const std::string& operation) const;
    void startLocalMcp(int port = 0);
private:
    struct State;
    std::unique_ptr<State> state_;
};
