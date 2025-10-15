#pragma once

class LootManager : public RE::BSTEventSink<RE::TESDeathEvent>,
                    public RE::BSTEventSink<RE::TESOpenCloseEvent> {
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
        const RE::TESOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::TESOpenCloseEvent>* a_eventSource) override;

private:
    LootManager() = default;
    ~LootManager() = default;
    
    LootManager(const LootManager&) = delete;
    LootManager(LootManager&&) = delete;
    LootManager& operator=(const LootManager&) = delete;
    LootManager& operator=(LootManager&&) = delete;
    
    void ProcessActorDeath(RE::Actor* a_actor);
    bool ShouldProcessActor(RE::Actor* a_actor);
    void DetermineLootableItems(RE::Actor* a_actor);
    bool ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    float GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    void RemoveNonLootableItems(RE::Actor* a_actor);
    void RestoreNonLootableItems(RE::Actor* a_actor);
    
    struct LootableItemData {
        std::unordered_set<RE::FormID> lootableItems;
        std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> removedItems;
        std::chrono::steady_clock::time_point timestamp;
    };
    
    std::unordered_map<RE::FormID, LootableItemData> actorLootData;
    std::mutex dataMutex;
    std::atomic<bool> enabled{true};
};