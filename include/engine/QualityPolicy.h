#pragma once
#include <algorithm>
#include <cstdint>
namespace engine {
// Explicit opt-in; pressure lowers a bounded tier and never changes CPU settings.
// Disabling it restores the requested settings on the next snapshot.
class QualityPolicy {
public:
    void setEnabled(bool enabled){enabled_=enabled;if(!enabled)level_=0;}
    bool onPressure(){if(!enabled_ || level_==3)return false;++level_;return true;}
    uint32_t level() const{return level_;}
    uint32_t fft(uint32_t requested) const{return level_?std::min(requested,1024u>>level_):requested;}
    uint32_t oceanMesh(uint32_t requested) const{return level_?std::min(requested,(256u>>level_)+1):requested;}
    uint32_t terrainLeaves(uint32_t requested) const{return level_?std::min(requested,2048u>>level_):requested;}
    uint32_t virtualColumns(uint32_t requested) const {const uint32_t caps[4]={16,6,4,2};return level_?std::min(requested,caps[level_]):requested;}
private:
    bool enabled_=false;uint32_t level_=0;
};
}
