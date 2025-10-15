#pragma once

struct LootData {
    std::unordered_set<RE::FormID> lootableItems;  // FormIDs of items that can be looted
    std::unordered_map<RE::FormID, std::int32_t> lootableCounts;  // How many of each
};

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

private:
    LootManager() = default;
    ~LootManager() = default;
    
    LootManager(const LootManager&) = delete;
    LootManager(LootManager&&) = delete;
    LootManager& operator=(const LootManager&) = delete;
    LootManager& operator=(LootManager&&) = delete;
    
    void ProcessActorDeath(RE::Actor* a_actor);
    bool ShouldProcessActor(RE::Actor* a_actor);
    LootData DetermineLootableItems(RE::Actor* a_actor);
    bool ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    float GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    bool IsItemLootable(RE::FormID actorID, RE::FormID itemID, std::int32_t count);
    void DecrementLootableCount(RE::FormID actorID, RE::FormID itemID, std::int32_t count);
    
    std::unordered_map<RE::FormID, LootData> lootDataMap;
    std::atomic<bool> enabled{true};
    std::mutex mapMutex;
};