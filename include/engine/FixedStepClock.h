#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace engine {
// Logic clock: GPU backpressure cannot turn one tick into an unbounded timestep.
class FixedStepClock {
public:
    explicit FixedStepClock(double step=1.0/60,uint32_t maxSteps=8):step_(step),maxSteps_(maxSteps) {
        if(!std::isfinite(step) || step<=0 || !maxSteps)throw std::invalid_argument("Invalid logic clock");
    }
    template<class F> uint32_t advance(double elapsed,F&& tick) {
        if(!std::isfinite(elapsed) || elapsed<0)throw std::invalid_argument("Invalid elapsed logic time");
        if(paused_)return 0;
        accumulator_+=elapsed*speed_;
        uint32_t count=0;
        while(accumulator_+step_*1e-10>=step_ && count<maxSteps_) {
            tick(step_);accumulator_=std::max(0.0,accumulator_-step_);time_+=step_;++count;
        }
        if(accumulator_>=step_) {
            const auto skipped=std::floor(accumulator_/step_);dropped_+=skipped*step_;accumulator_-=skipped*step_;
        }
        return count;
    }
    void setPaused(bool paused) {paused_=paused;}
    void setSpeed(double speed) {if(!std::isfinite(speed) || speed<0 || speed>8)throw std::invalid_argument("Invalid simulation speed");speed_=speed;}
    double seconds() const {return time_;}
    double interpolation() const {return accumulator_/step_;}
    double droppedSeconds() const {return dropped_;}
private:
    double step_,accumulator_=0,time_=0,dropped_=0,speed_=1;
    uint32_t maxSteps_;
    bool paused_=false;
};
}
