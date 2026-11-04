#include "LooperBridge.h"

#include <algorithm>
#include <map>

namespace LoopTexturer {

namespace {

std::mutex gRegistryMutex;
int64_t gNextId = 1;

std::map<int64_t, std::weak_ptr<LooperBridge>>& getRegistry ()
{
    static std::map<int64_t, std::weak_ptr<LooperBridge>> registry;
    return registry;
}

} // namespace

std::shared_ptr<LooperBridge> LooperBridge::create ()
{
    std::shared_ptr<LooperBridge> bridge (new LooperBridge);

    const std::lock_guard<std::mutex> lock (gRegistryMutex);
    auto& registry = getRegistry ();

    // Instances come and go over a session, so expired entries are removed here.
    for (auto it = registry.begin (); it != registry.end ();)
        it = it->second.expired () ? registry.erase (it) : std::next (it);

    bridge->_id = gNextId++;
    registry[bridge->_id] = bridge;
    return bridge;
}

std::shared_ptr<LooperBridge> LooperBridge::find (int64_t id)
{
    const std::lock_guard<std::mutex> lock (gRegistryMutex);
    const auto& registry = getRegistry ();
    const auto it = registry.find (id);
    return it == registry.end () ? nullptr : it->second.lock ();
}

void LooperBridge::addTape (const std::shared_ptr<Tape>& tape)
{
    const std::lock_guard<std::mutex> lock (_tapesMutex);
    _tapes.push_back (tape);
}

void LooperBridge::releaseTapesExcept (const std::vector<const Tape*>& inUse)
{
    std::vector<std::shared_ptr<Tape>> released;
    {
        const std::lock_guard<std::mutex> lock (_tapesMutex);
        const auto isUnused = [&inUse] (const std::shared_ptr<Tape>& tape) {
            return std::find (inUse.begin (), inUse.end (), tape.get ()) == inUse.end ();
        };
        for (const auto& tape : _tapes)
        {
            if (isUnused (tape))
                released.push_back (tape);
        }
        _tapes.erase (std::remove_if (_tapes.begin (), _tapes.end (), isUnused), _tapes.end ());
    }
    // The memory is freed here, outside the lock, because large tapes take a moment to free.
}

std::shared_ptr<Tape> LooperBridge::getActiveTape () const
{
    const Tape* active = status.activeTape.load ();
    const std::lock_guard<std::mutex> lock (_tapesMutex);
    for (const auto& tape : _tapes)
    {
        if (tape.get () == active)
            return tape;
    }
    return nullptr;
}

} // namespace LoopTexturer
