#pragma once

class LootManager : public RE::BSTEventSink<RE::TESDeathEvent> {
public:
    static LootManager* GetSingleton() {
        static LootManager singleton;
        return &singleton;
    }
    
    void Register();
    
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESDeathEvent* a_event,
        RE::BSTEventSource<RE::TESDeathEvent>* a_eventSource) override;

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
    void FilterCorpseInventory(RE::Actor* a_actor, const std::unordered_set<RE::FormID>& lootableItems);
    bool IsItemEquipped(RE::Actor* a_actor, RE::TESBoundObject* a_item);
    
    std::mutex processingMutex;
    std::atomic<bool> enabled{true};
};