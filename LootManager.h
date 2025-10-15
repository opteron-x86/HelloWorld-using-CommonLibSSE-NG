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
    
    struct DroppedItem {
        RE::TESBoundObject* item;
        std::int32_t count;
    };
    
    void ProcessActorDeath(RE::Actor* a_actor, RE::Actor* a_killer);
    bool ShouldProcessActor(RE::Actor* a_actor);
    std::vector<DroppedItem> RollForLoot(RE::Actor* a_actor);
    void DropLootPhysically(RE::Actor* a_actor, const std::vector<DroppedItem>& items);
    void CleanInventory(RE::Actor* a_actor, const std::vector<DroppedItem>& droppedItems);
    
    bool ShouldDropItem(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    bool IsEssentialItem(RE::TESBoundObject* a_item);
    float GetDropChance(RE::TESBoundObject* a_item, RE::Actor* a_actor);
    
    std::atomic<bool> enabled{true};
    std::mutex processingMutex;
};