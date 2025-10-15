#pragma once

class LootManager : 
    public RE::BSTEventSink<RE::TESDeathEvent>,
    public RE::BSTEventSink<RE::TESActivateEvent>,
    public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
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
        const RE::TESActivateEvent* a_event,
        RE::BSTEventSource<RE::TESActivateEvent>* a_eventSource) override;
    
    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

private:
    struct LootData {
        std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> allowedItems;
        RE::ObjectRefHandle containerHandle;
        bool hasContainer = false;
    };
    
    LootManager() = default;
    ~LootManager() = default;
    
    LootManager(const LootManager&) = delete;
    LootManager(LootManager&&) = delete;
    LootManager& operator=(const LootManager&) = delete;
    LootManager& operator=(LootManager&&) = delete;
    
    void ProcessActorDeath(RE::Actor* a_actor);
    bool ShouldProcessActor(RE::Actor* a_actor);
    void DetermineAllowedLoot(RE::Actor* a_actor);
    bool ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    float GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    
    void HandleCorpseActivation(RE::Actor* a_corpse, RE::Actor* a_activator);
    void CreateLootContainer(RE::Actor* a_corpse, RE::Actor* a_activator);
    void CleanupContainer(RE::FormID a_corpseID);
    
    std::atomic<bool> enabled{true};
    std::mutex dataMutex;
    std::unordered_map<RE::FormID, LootData> lootDataMap;
    RE::FormID activeCorpseID = 0;
};