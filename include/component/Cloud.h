#pragma once
#include "component/Component.h"
#include "component/CloudSettings.h"
class Cloud : public Component {
public:
    Cloud(){name="Cloud";}
    render::CloudSettings settings() const {checkLogicThread();return settings_;}
    void setSettings(const render::CloudSettings& value){checkLogicThread();value.validate();if(!(value==settings_)){settings_=value;invalidate();}}
    template<class F> void updateSettings(F&& edit){auto value=settings();edit(value);setSettings(value);}
    void loadFromJson(json&) override;
private:
    render::CloudSettings settings_;
};
