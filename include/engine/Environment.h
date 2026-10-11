#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <array>
#include <memory>

namespace engine {
struct EnvironmentSolar {
    double elevationDegrees=10, azimuthDegrees=0;
};
struct EnvironmentWind { double x=12, z=4; }; // World XZ metres/second.
// Horizontal depths/rates in metres and metres/second.
struct SurfaceWaterConfig {
    double areaSquareMeters=1, capacityMeters=.002, infiltrationMetersPerSecond=.5/3600000,
           evaporationMetersPerSecond=.1/3600000, initialStorageMeters=0;
};
struct SurfaceWaterAccount {
    double storageMeters=0, rainMeters=0, infiltrationMeters=0, evaporationMeters=0, outflowMeters=0;
};
// Fixed coarse terrain cells in source height UV order. No GPU/readback state.
struct SurfaceWaterGrid {
    static constexpr size_t width=16, cells=width*width;
    std::array<double,cells> areaSquareMeters{}, elevationMeters{};
    double cellXMeters=1, cellZMeters=1, runoffPerSecond=.05;
};
struct EnvironmentConfig {
    std::string sceneKey;
    double stepSeconds=1.0/60, initialSeconds=0;
    EnvironmentSolar initialSolar;
    EnvironmentWind initialWind;
    double initialCloudCoverage=.55;
    double initialRainMetersPerSecond=0, lakeAreaSquareMeters=0;
    SurfaceWaterConfig land;
    std::shared_ptr<const SurfaceWaterGrid> landGrid;
};
// Detached CPU values. This is not a checkpoint of GPU shallow-water history.
struct EnvironmentSnapshot {
    bool enabled=false;
    uint64_t epoch=1, revision=1, tick=0, appliedSequence=0;
    double seconds=0;
    EnvironmentSolar solar;
    EnvironmentWind wind, windTarget, waveFrom, waveTo;
    double cloudX=0, cloudZ=0, grassPhase=0, waveElapsed=8;
    double cloudCoverage=.55, coverageFrom=.55, coverageTarget=.55, coverageElapsed=6;
    double rainMetersPerSecond=0, lakeRainCubicMeters=0;
    SurfaceWaterAccount land;
    double landWetness=0;
    std::shared_ptr<const SurfaceWaterGrid> landGrid;
    std::array<double,SurfaceWaterGrid::cells> landStorageMeters{};
    double landTransferredCubicMeters=0;
    uint32_t pendingEvents=0;
};
class Environment {
public:
    static constexpr size_t eventCapacity=64;
    explicit Environment(EnvironmentConfig);
    Environment(const Environment&)=delete;
    Environment& operator=(const Environment&)=delete;
    void reset(EnvironmentConfig);
    void setEnabled(bool);
    // Event ticks must be future and submitted in nondecreasing order. Queue
    // overflow rejects the operation without assigning a sequence or mutating state.
    uint64_t enqueueSolar(EnvironmentSolar,uint64_t targetTick);
    uint64_t enqueueWind(EnvironmentWind,uint64_t targetTick);
    EnvironmentWind requestedWind() const;
    uint64_t enqueueCoverage(double,uint64_t targetTick);
    double requestedCoverage() const;
    uint64_t enqueueRain(double metersPerSecond,uint64_t targetTick);
    double requestedRain() const;
    void step(); // Exactly one configured tick; pause/speed are caller policies.
    EnvironmentSnapshot snapshot() const;
    EnvironmentSolar requestedSolar() const; // Latest accepted intent, including paused/future events.
    const EnvironmentConfig& config() const;
    std::string checkpoint() const;
    void restore(const std::string&); // Fully validate before replacing CPU state.
    void save(const std::string& path) const;
    void load(const std::string& path);
private:
    enum class Kind { Solar, Wind, Coverage, Rain };
    struct Event {uint64_t sequence=0,tick=0;EnvironmentSolar solar;Kind kind=Kind::Solar;EnvironmentWind wind;double coverage=.55,rain=0;};
    uint64_t enqueue(Event);
    void checkOwner() const;
    void stepWater(double dt);
    std::thread::id owner_=std::this_thread::get_id();
    EnvironmentConfig config_;
    EnvironmentSnapshot state_;
    uint64_t nextSequence_=1;
    std::deque<Event> events_;
};
}
