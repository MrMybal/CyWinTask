#pragma once

struct ID3D11Device;
class AppUi {
public:
    explicit AppUi(ID3D11Device* device, bool demoMode = false, bool performanceAtStart = false,
                   bool assistantAtStart = false, int mcpLocalPort = -1, int utilityAtStart = 0,
                   bool performanceDetailsAtStart = false);
    ~AppUi();
    void frame();
private:
    struct State;
    State* state_;
};
