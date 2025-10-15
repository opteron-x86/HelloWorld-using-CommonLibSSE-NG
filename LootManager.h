#pragma once

class LootManager : public RE::BSTEventSink<RE::TESDeathEvent>,
                    public RE::BSTEventSink<RE::TESContainerChangedEvent> {
public:
    static LootManager* GetSingleton() {
        static LootManager singleton;
        return &singleton;
    }
    
    void Register();
    
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESDeathEvent* a_event,
        RE::BSTEventSource<RE::TESDeathEvent>* a_eventSource) override;
    
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESContainerChangedEvent* a_event,
        RE::BSTEventSource<RE::TESContainerChangedEvent>* a_eventSource) override;

    bool IsItemLootable(RE::FormID a_containerID, RE::TESBoundObject* a_item);
    
private:
    LootManager() = default;
    ~LootManager() = default;
    
    LootManager(const LootManager&) = delete;
    LootManager(LootManager&&) = delete;
    LootManager& operator=(const LootManager&) = delete;
    LootManager& operator=(LootManager&&) = delete;
    
    void ProcessActorDeath(RE::Actor* a_actor, RE::Actor* a_killer);
    bool ShouldProcessActor(RE::Actor* a_actor);
    void MarkUnlootableItems(RE::Actor* a_actor);
    bool ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    float GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    
    struct LootableItems {
        std::unordered_set<RE::FormID> items;
        std::chrono::steady_clock::time_point timestamp;
    };
    
    std::unordered_map<RE::FormID, LootableItems> actorLootMap;
    std::mutex mapMutex;
    std::atomic<bool> enabled{true};
};