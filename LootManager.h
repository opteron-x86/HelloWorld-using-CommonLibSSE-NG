#pragma once

class LootManager : public RE::BSTEventSink<RE::TESDeathEvent>,
                   public RE::BSTEventSink<RE::TESActivateEvent> {
public:
    static LootManager* GetSingleton() {
        static LootManager singleton;
        return &singleton;
    }
    
    void Register();
    
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESDeathEvent* a_event,
        RE::BSTEventSource<RE::TESDeathEvent>*) override;
        
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESActivateEvent* a_event,
        RE::BSTEventSource<RE::TESActivateEvent>*) override;

private:
    LootManager() = default;
    ~LootManager() = default;
    
    LootManager(const LootManager&) = delete;
    LootManager(LootManager&&) = delete;
    LootManager& operator=(const LootManager&) = delete;
    LootManager& operator=(LootManager&&) = delete;
    
    struct RolledLoot {
        std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> items;
        bool processed = false;
        bool looted = false;
    };
    
    std::unordered_map<RE::FormID, RolledLoot> rolledLootMap;
    std::mutex mapMutex;
    
    void ProcessActorDeath(RE::Actor* a_actor, RE::Actor* a_killer);
    bool ShouldProcessActor(RE::Actor* a_actor);
    void RollLootForActor(RE::Actor* a_actor);
    bool ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    float GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    
    void HandleCorpseActivation(RE::Actor* a_corpse, RE::TESObjectREFR* a_activator);
    RE::TESObjectREFR* CreateTempContainer(RE::Actor* a_corpse);
    void TransferLootToContainer(RE::Actor* a_corpse, RE::TESObjectREFR* a_container, const RolledLoot& a_loot);
    void CleanupContainer(RE::TESObjectREFR* a_container);
    
    std::atomic<bool> enabled{true};
};