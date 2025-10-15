#pragma once

class LootManager : 
    public RE::BSTEventSink<RE::TESDeathEvent>,
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
    
    void ProcessActorDeath(RE::Actor* a_actor, RE::Actor* a_killer);
    bool ShouldProcessActor(RE::Actor* a_actor);
    void FilterInventory(RE::Actor* a_actor);
    bool ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    float GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    bool IsItemEquipped(RE::Actor* a_actor, RE::TESBoundObject* a_item);
    
    void BlockItem(RE::Actor* a_actor, RE::TESBoundObject* a_item);
    bool IsItemBlocked(RE::Actor* a_actor, RE::TESBoundObject* a_item);
    void CleanupBlockedItems(RE::Actor* a_actor);
    
    std::atomic<bool> enabled{true};
    std::mutex processingMutex;
    
    // Track items that should remain equipped but unlootable
    // Map: ActorFormID -> Set of ItemFormIDs
    std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>> blockedItems;
    std::mutex blockedItemsMutex;
};